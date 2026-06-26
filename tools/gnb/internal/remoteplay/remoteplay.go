package remoteplay

import (
	"fmt"
	"net"
	"sort"
	"strings"
)

type Options struct {
	Bind          string
	Resolution    string
	Encoder       string
	HttpPort      uint32
	SignalingPort uint32
	BitrateKbps   uint32
	Fps           uint32
	ShowWindow    bool
	Scene         string
}

func RunArgs(opts Options, trailingArgs []string) []string {
	runArgs := []string{
		"--remote",
		fmt.Sprintf("--remote-bind=%s", strings.TrimSpace(opts.Bind)),
		fmt.Sprintf("--remote-http-port=%d", opts.HttpPort),
		fmt.Sprintf("--remote-port=%d", opts.SignalingPort),
		fmt.Sprintf("--remote-bitrate=%d", opts.BitrateKbps),
		fmt.Sprintf("--remote-fps=%d", opts.Fps),
		fmt.Sprintf("--remote-encoder=%s", strings.TrimSpace(opts.Encoder)),
	}
	if value := strings.TrimSpace(opts.Resolution); value != "" {
		runArgs = append(runArgs, "--remote-res="+value)
	}
	if value := strings.TrimSpace(opts.Scene); value != "" {
		runArgs = append(runArgs, "--load-scene="+value)
	}
	if opts.ShowWindow {
		runArgs = append(runArgs, "--remote-show-window")
	}
	return append(runArgs, trailingArgs...)
}

func BuildAccessURLs(bind string, httpPort uint32, discoveredHosts []string) []string {
	bind = strings.TrimSpace(bind)
	if bind != "" && bind != "0.0.0.0" && bind != "::" {
		return []string{fmt.Sprintf("http://%s:%d", FormatURLHost(bind), httpPort)}
	}
	hosts := make([]string, 0, len(discoveredHosts)+2)
	hosts = append(hosts, "127.0.0.1", "localhost")
	sortedHosts := append([]string(nil), discoveredHosts...)
	sort.Strings(sortedHosts)
	hosts = append(hosts, sortedHosts...)
	urls := make([]string, 0, len(hosts))
	seen := make(map[string]struct{}, len(hosts))
	for _, host := range hosts {
		host = strings.TrimSpace(host)
		if host == "" {
			continue
		}
		if _, ok := seen[host]; ok {
			continue
		}
		seen[host] = struct{}{}
		urls = append(urls, fmt.Sprintf("http://%s:%d", FormatURLHost(host), httpPort))
	}
	return urls
}

func LocalIPv4Hosts() []string {
	interfaces, err := net.Interfaces()
	if err != nil {
		return nil
	}
	privateHosts := make([]string, 0, 4)
	otherHosts := make([]string, 0, 2)
	seen := make(map[string]struct{})
	for _, iface := range interfaces {
		if iface.Flags&net.FlagUp == 0 || iface.Flags&net.FlagLoopback != 0 {
			continue
		}
		addrs, err := iface.Addrs()
		if err != nil {
			continue
		}
		for _, addr := range addrs {
			var ip net.IP
			switch value := addr.(type) {
			case *net.IPNet:
				ip = value.IP
			case *net.IPAddr:
				ip = value.IP
			}
			ip = ip.To4()
			if ip == nil || ip.IsLoopback() || ip.IsLinkLocalUnicast() {
				continue
			}
			host := ip.String()
			if _, ok := seen[host]; ok {
				continue
			}
			seen[host] = struct{}{}
			if ip.IsPrivate() {
				privateHosts = append(privateHosts, host)
			} else {
				otherHosts = append(otherHosts, host)
			}
		}
	}
	if len(privateHosts) > 0 {
		return privateHosts
	}
	return otherHosts
}

func FormatURLHost(host string) string {
	if ip := net.ParseIP(host); ip != nil && ip.To4() == nil {
		return "[" + host + "]"
	}
	return host
}
