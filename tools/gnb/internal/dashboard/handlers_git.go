// Git tab: status, branch switch/create, pull/fetch, reset, stash, staging,
// commit-message generation, and commit creation. All git plumbing is in the
// gitops package; these handlers only translate HTTP <-> gitops + render.
package dashboard

import (
	"context"
	"encoding/json"
	"fmt"
	"html"
	"net/http"
	"strings"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/workflow/commitmessage"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/gitops"
)

func (s *Server) buildGitVM(flash string) gitVM {
	vm := gitVM{Flash: flash}
	st, err := gitops.GetStatus(s.opts.RepoRoot)
	if err != nil {
		vm.Error = err.Error()
		return vm
	}
	vm.Status = st
	if branches, err := gitops.Branches(s.opts.RepoRoot); err != nil {
		vm.Error = err.Error()
	} else {
		vm.Branches = branches
	}
	if remotes, err := gitops.RemoteBranches(s.opts.RepoRoot); err != nil {
		if vm.Error == "" {
			vm.Error = err.Error()
		}
	} else {
		vm.RemoteBranches = remotes
	}
	if commits, err := gitops.Log(s.opts.RepoRoot, 30); err != nil {
		if vm.Error == "" {
			vm.Error = err.Error()
		}
	} else {
		vm.Commits = commits
	}
	if st.Upstream != "" {
		if commits, err := gitops.LogRange(s.opts.RepoRoot, "HEAD.."+st.Upstream, 12); err != nil {
			if vm.Error == "" {
				vm.Error = err.Error()
			}
		} else {
			vm.RemoteCommits = commits
		}
	}
	if stashes, err := gitops.StashList(s.opts.RepoRoot); err != nil {
		if vm.Error == "" {
			vm.Error = err.Error()
		}
	} else {
		vm.Stashes = stashes
	}
	return vm
}

// renderGitBody re-renders the whole git tab body (left column + commits +
// stash). All destructive actions go through this so both columns stay in
// sync (e.g. reset moves HEAD which changes the log).
func (s *Server) renderGitBody(w http.ResponseWriter, flash string) {
	vm := s.buildHeader("git")
	vm.GitVM = s.buildGitVM(flash)
	s.render(w, "git_body", vm)
}

func (s *Server) handleGitPanel(w http.ResponseWriter, r *http.Request) {
	s.renderGitBody(w, r.URL.Query().Get("flash"))
}

func (s *Server) handleGitSwitch(w http.ResponseWriter, r *http.Request) {
	if err := r.ParseForm(); err != nil {
		httpError(w, err)
		return
	}
	branch := strings.TrimSpace(r.FormValue("branch"))
	if branch == "" {
		http.Error(w, "branch is required", http.StatusBadRequest)
		return
	}
	if err := gitops.Checkout(s.opts.RepoRoot, branch, false); err != nil {
		s.renderGitBody(w, "切换分支失败: "+err.Error())
		return
	}
	s.renderGitBody(w, "已切换到 "+branch)
}

func (s *Server) handleGitPull(w http.ResponseWriter, r *http.Request) {
	out, err := gitops.Pull(s.opts.RepoRoot)
	if err != nil {
		s.renderGitBody(w, "Pull 失败: "+err.Error())
		return
	}
	flash := "Pull 完成"
	if first := firstLine(out); first != "" {
		flash = flash + " (" + first + ")"
	}
	s.renderGitBody(w, flash)
}

func (s *Server) handleGitFetch(w http.ResponseWriter, r *http.Request) {
	if _, err := gitops.Fetch(s.opts.RepoRoot); err != nil {
		s.renderGitBody(w, "Fetch 失败: "+err.Error())
		return
	}
	s.renderGitBody(w, "Fetch 完成")
}

func (s *Server) handleGitSwitchRemote(w http.ResponseWriter, r *http.Request) {
	if err := r.ParseForm(); err != nil {
		httpError(w, err)
		return
	}
	ref := strings.TrimSpace(r.FormValue("ref"))
	if ref == "" {
		http.Error(w, "ref required", http.StatusBadRequest)
		return
	}
	if err := gitops.CheckoutRemote(s.opts.RepoRoot, ref, false); err != nil {
		s.renderGitBody(w, "切换远程分支失败: "+err.Error())
		return
	}
	s.renderGitBody(w, "已基于 "+ref+" 创建本地跟踪分支")
}

func (s *Server) handleGitCreateBranch(w http.ResponseWriter, r *http.Request) {
	if err := r.ParseForm(); err != nil {
		httpError(w, err)
		return
	}
	name := strings.TrimSpace(r.FormValue("name"))
	startPoint := strings.TrimSpace(r.FormValue("start"))
	if name == "" {
		http.Error(w, "name required", http.StatusBadRequest)
		return
	}
	if err := gitops.CreateBranch(s.opts.RepoRoot, name, startPoint, false); err != nil {
		s.renderGitBody(w, "创建分支失败: "+err.Error())
		return
	}
	s.renderGitBody(w, "已创建并切换到 "+name)
}

func (s *Server) handleGitReset(w http.ResponseWriter, r *http.Request) {
	if err := r.ParseForm(); err != nil {
		httpError(w, err)
		return
	}
	ref := strings.TrimSpace(r.FormValue("ref"))
	if ref == "" {
		http.Error(w, "ref required", http.StatusBadRequest)
		return
	}
	if err := gitops.ResetHard(s.opts.RepoRoot, ref); err != nil {
		s.renderGitBody(w, "Reset 失败: "+err.Error())
		return
	}
	s.renderGitBody(w, "已 reset --hard 到 "+ref)
}

func (s *Server) handleGitStashPush(w http.ResponseWriter, r *http.Request) {
	if err := r.ParseForm(); err != nil {
		httpError(w, err)
		return
	}
	msg := strings.TrimSpace(r.FormValue("message"))
	includeUntracked := r.FormValue("untracked") == "1"
	out, err := gitops.StashPush(s.opts.RepoRoot, msg, includeUntracked)
	if err != nil {
		s.renderGitBody(w, "Stash 失败: "+err.Error())
		return
	}
	flash := "已 stash"
	if first := firstLine(out); first != "" {
		flash = flash + " (" + first + ")"
	}
	s.renderGitBody(w, flash)
}

func (s *Server) handleGitStashAction(w http.ResponseWriter, r *http.Request) {
	action := r.PathValue("action")
	if err := r.ParseForm(); err != nil {
		httpError(w, err)
		return
	}
	ref := strings.TrimSpace(r.FormValue("ref"))
	out, err := gitops.StashAction(s.opts.RepoRoot, action, ref)
	if err != nil {
		s.renderGitBody(w, "stash "+action+" 失败: "+err.Error())
		return
	}
	flash := "stash " + action + " 完成"
	if first := firstLine(out); first != "" {
		flash = flash + " (" + first + ")"
	}
	s.renderGitBody(w, flash)
}

func (s *Server) handleGitCommit(w http.ResponseWriter, r *http.Request) {
	ref := r.PathValue("ref")
	if ref == "" {
		http.Error(w, "ref required", http.StatusBadRequest)
		return
	}
	c, err := gitops.ShowCommit(s.opts.RepoRoot, ref)
	if err != nil {
		http.Error(w, err.Error(), http.StatusNotFound)
		return
	}
	s.render(w, "git_commit_detail", c)
}

func (s *Server) handleGitLocalChanges(w http.ResponseWriter, r *http.Request) {
	vm := s.buildHeader("git")
	vm.GitVM = s.buildGitVM(r.URL.Query().Get("flash"))
	s.render(w, "git_commit_card", vm)
}

func (s *Server) handleGitStage(w http.ResponseWriter, r *http.Request) {
	if err := r.ParseForm(); err != nil {
		httpError(w, err)
		return
	}
	path := strings.TrimSpace(r.FormValue("path"))
	if path == "" {
		if err := gitops.AddAll(s.opts.RepoRoot); err != nil {
			s.renderGitBody(w, "Stage 失败: "+err.Error())
			return
		}
		s.renderGitBody(w, "已 stage 全部改动")
		return
	}
	if err := gitops.AddPath(s.opts.RepoRoot, path); err != nil {
		s.renderGitBody(w, "Stage 失败: "+err.Error())
		return
	}
	s.renderGitBody(w, "已 stage: "+path)
}

func (s *Server) handleGitUnstage(w http.ResponseWriter, r *http.Request) {
	if err := r.ParseForm(); err != nil {
		httpError(w, err)
		return
	}
	path := strings.TrimSpace(r.FormValue("path"))
	if path == "" {
		if err := gitops.UnstageAll(s.opts.RepoRoot); err != nil {
			s.renderGitBody(w, "Unstage 失败: "+err.Error())
			return
		}
		s.renderGitBody(w, "已 unstage 全部改动")
		return
	}
	if err := gitops.UnstagePath(s.opts.RepoRoot, path); err != nil {
		s.renderGitBody(w, "Unstage 失败: "+err.Error())
		return
	}
	s.renderGitBody(w, "已 unstage: "+path)
}

func firstLine(s string) string {
	if i := strings.IndexByte(s, '\n'); i >= 0 {
		return s[:i]
	}
	return s
}

// handleGitCommitMessage runs the LLM commit-message generator and returns
// an HTMX fragment that replaces the textarea inside the commit card.
//
// The endpoint takes the LLM round-trip in the request goroutine (can take
// 30-60s on first call while the model loads). HTMX's hx-indicator handles
// the spinner; the client times out at 5 min just like the CLI.
func (s *Server) handleGitCommitMessage(w http.ResponseWriter, r *http.Request) {
	ctx, cancel := context.WithTimeout(r.Context(), 5*time.Minute)
	defer cancel()
	runtime, err := ai.NewRuntime(s.opts.RepoRoot, s.opts.Config)
	if err != nil {
		writeCommitTextarea(w, "[AI error] "+err.Error())
		return
	}
	rawResult, err := runtime.Workflows.Run(ctx, commitmessage.Name, commitmessage.Input{MaxDiffChars: 16000, Temperature: .2, Profile: "general"}, nil)
	var res commitmessage.Output
	if err == nil {
		err = json.Unmarshal(rawResult, &res)
	}
	if err != nil {
		// Surface error inside the textarea so the user can see what went wrong
		// without breaking the rest of the page.
		writeCommitTextarea(w, "[LLM error] "+err.Error())
		return
	}
	writeCommitTextarea(w, res.Message)
}

// handleGitCommitCreate stages all (optional) and commits with the provided
// message. Re-renders the whole git body so the recent-commits list and dirty
// state reflect the new HEAD.
func (s *Server) handleGitCommitCreate(w http.ResponseWriter, r *http.Request) {
	if err := r.ParseForm(); err != nil {
		httpError(w, err)
		return
	}
	message := strings.TrimSpace(r.FormValue("message"))
	if message == "" {
		s.renderGitBody(w, "Commit 失败: 提交消息为空")
		return
	}
	if r.FormValue("stage_all") == "1" {
		if err := gitops.AddAll(s.opts.RepoRoot); err != nil {
			s.renderGitBody(w, "git add -A 失败: "+err.Error())
			return
		}
	}
	if _, err := gitops.CreateCommit(s.opts.RepoRoot, message); err != nil {
		s.renderGitBody(w, "Commit 失败: "+err.Error())
		return
	}
	subject := firstLine(message)
	if len(subject) > 60 {
		subject = subject[:60] + "..."
	}
	s.renderGitBody(w, "已提交: "+subject)
}

// writeCommitTextarea emits the textarea HTML fragment used as the htmx swap
// target. Keeping the markup here (rather than a separate template) keeps the
// fragment trivially small and lets the caller embed it without a render
// round-trip.
func writeCommitTextarea(w http.ResponseWriter, content string) {
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	fmt.Fprintf(w,
		`<textarea id="commit-msg-textarea" name="message" class="input commit-msg-textarea" rows="10" placeholder="commit message">%s</textarea>`,
		html.EscapeString(content),
	)
}
