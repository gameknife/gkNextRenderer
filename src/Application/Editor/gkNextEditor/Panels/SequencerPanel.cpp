#include "EditorUi.hpp"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Command/ICommand.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"

#include <fmt/format.h>

namespace Editor
{
    namespace
    {
        using EChannel = EditorUiState::SequencerState::EChannel;

        constexpr float kTrackColumnWidth = 230.0f;
        constexpr float kInspectorColumnWidth = 270.0f;
        constexpr float kRulerHeight = 30.0f;
        constexpr float kRowHeight = 24.0f;
        constexpr float kKeyRadius = 5.0f;

        const char* ChannelLabel(const EChannel channel)
        {
            switch (channel)
            {
            case EChannel::Translation: return "Translation";
            case EChannel::Rotation: return "Rotation";
            case EChannel::Scale: return "Scale";
            case EChannel::SunRotation: return "Sun Rotation";
            case EChannel::SunElevation: return "Sun Elevation";
            case EChannel::SkyRotation: return "Sky Rotation";
            case EChannel::SunIntensity: return "Sun Intensity";
            case EChannel::SkyIntensity: return "Sky Intensity";
            case EChannel::SunColor: return "Sun Color";
            case EChannel::SkyColor: return "Sky Color";
            }
            return "Channel";
        }

        ImU32 ChannelColor(const EChannel channel, const bool selected)
        {
            ImVec4 color;
            switch (channel)
            {
            case EChannel::Translation: color = ImVec4(0.30f, 0.72f, 1.00f, 1.0f); break;
            case EChannel::Rotation: color = ImVec4(1.00f, 0.58f, 0.26f, 1.0f); break;
            case EChannel::Scale: color = ImVec4(0.48f, 0.88f, 0.48f, 1.0f); break;
            case EChannel::SunColor: color = ImVec4(1.00f, 0.70f, 0.30f, 1.0f); break;
            case EChannel::SkyColor: color = ImVec4(0.48f, 0.65f, 1.00f, 1.0f); break;
            default: color = ImVec4(0.86f, 0.72f, 0.30f, 1.0f); break;
            }
            if (!selected)
            {
                color.w = 0.88f;
            }
            return ImGui::ColorConvertFloat4ToU32(color);
        }

        std::span<const EChannel> ChannelsForTrack(const Assets::AnimationTrack& track)
        {
            static constexpr std::array transformChannels{
                EChannel::Translation, EChannel::Rotation, EChannel::Scale,
            };
            static constexpr std::array environmentChannels{
                EChannel::SunRotation, EChannel::SunElevation, EChannel::SkyRotation,
                EChannel::SunIntensity, EChannel::SkyIntensity, EChannel::SunColor, EChannel::SkyColor,
            };
            return track.Target_ == Assets::AnimationTrack::Target::Environment
                ? std::span<const EChannel>(environmentChannels)
                : std::span<const EChannel>(transformChannels);
        }

        template <typename Fn>
        void ForEachKeyTime(const Assets::AnimationTrack& track, const EChannel channel, Fn&& fn)
        {
            const auto visit = [&fn](const auto& typedChannel)
            {
                for (int index = 0; index < static_cast<int>(typedChannel.Keys.size()); ++index)
                {
                    fn(index, typedChannel.Keys[index].Time);
                }
            };
            switch (channel)
            {
            case EChannel::Translation: visit(track.TranslationChannel); break;
            case EChannel::Rotation: visit(track.RotationChannel); break;
            case EChannel::Scale: visit(track.ScaleChannel); break;
            case EChannel::SunRotation: visit(track.SunRotationChannel); break;
            case EChannel::SunElevation: visit(track.SunElevationChannel); break;
            case EChannel::SkyRotation: visit(track.SkyRotationChannel); break;
            case EChannel::SunIntensity: visit(track.SunIntensityChannel); break;
            case EChannel::SkyIntensity: visit(track.SkyIntensityChannel); break;
            case EChannel::SunColor: visit(track.SunColorChannel); break;
            case EChannel::SkyColor: visit(track.SkyColorChannel); break;
            }
        }

        int KeyCount(const Assets::AnimationTrack& track, const EChannel channel)
        {
            int count = 0;
            ForEachKeyTime(track, channel, [&count](int, float) { ++count; });
            return count;
        }

        float* KeyTime(Assets::AnimationTrack& track, const EChannel channel, const int keyIndex)
        {
            const auto get = [keyIndex](auto& typedChannel) -> float*
            {
                return keyIndex >= 0 && keyIndex < static_cast<int>(typedChannel.Keys.size())
                    ? &typedChannel.Keys[keyIndex].Time
                    : nullptr;
            };
            switch (channel)
            {
            case EChannel::Translation: return get(track.TranslationChannel);
            case EChannel::Rotation: return get(track.RotationChannel);
            case EChannel::Scale: return get(track.ScaleChannel);
            case EChannel::SunRotation: return get(track.SunRotationChannel);
            case EChannel::SunElevation: return get(track.SunElevationChannel);
            case EChannel::SkyRotation: return get(track.SkyRotationChannel);
            case EChannel::SunIntensity: return get(track.SunIntensityChannel);
            case EChannel::SkyIntensity: return get(track.SkyIntensityChannel);
            case EChannel::SunColor: return get(track.SunColorChannel);
            case EChannel::SkyColor: return get(track.SkyColorChannel);
            }
            return nullptr;
        }

        template <typename T>
        int InsertKey(Assets::AnimationChannel<T>& channel, const float time, const T& value)
        {
            constexpr float epsilon = 0.0001f;
            for (int index = 0; index < static_cast<int>(channel.Keys.size()); ++index)
            {
                if (std::abs(channel.Keys[index].Time - time) <= epsilon)
                {
                    channel.Keys[index].Value = value;
                    return index;
                }
            }
            const auto position = std::lower_bound(channel.Keys.begin(), channel.Keys.end(), time,
                [](const Assets::AnimationKey<T>& key, const float value) { return key.Time < value; });
            return static_cast<int>(channel.Keys.insert(position, {time, value}) - channel.Keys.begin());
        }

        int AddKeyAt(EditorContext& ctx, Assets::AnimationTrack& track, const EChannel channel, const float time)
        {
            Assets::Node* node = track.Target_ == Assets::AnimationTrack::Target::NodeTransform
                ? ctx.scene.GetNode(track.NodeName_)
                : nullptr;
            const Assets::EnvironmentSetting& environment = ctx.scene.GetEnvSettings();
            switch (channel)
            {
            case EChannel::Translation:
                return InsertKey(track.TranslationChannel, time, node ? node->Translation() : glm::vec3(0.0f));
            case EChannel::Rotation:
                return InsertKey(track.RotationChannel, time, node ? node->Rotation() : glm::quat(1, 0, 0, 0));
            case EChannel::Scale:
                return InsertKey(track.ScaleChannel, time, node ? node->Scale() : glm::vec3(1.0f));
            case EChannel::SunRotation:
                return InsertKey(track.SunRotationChannel, time, environment.SunRotation);
            case EChannel::SunElevation:
                return InsertKey(track.SunElevationChannel, time, environment.SunElevation);
            case EChannel::SkyRotation:
                return InsertKey(track.SkyRotationChannel, time, environment.SkyRotation);
            case EChannel::SunIntensity:
                return InsertKey(track.SunIntensityChannel, time, environment.SunIntensity);
            case EChannel::SkyIntensity:
                return InsertKey(track.SkyIntensityChannel, time, environment.SkyIntensity);
            case EChannel::SunColor:
                return InsertKey(track.SunColorChannel, time, environment.SunColor);
            case EChannel::SkyColor:
                return InsertKey(track.SkyColorChannel, time, environment.SkyColor);
            }
            return -1;
        }

        void EraseKey(Assets::AnimationTrack& track, const EChannel channel, const int keyIndex)
        {
            const auto erase = [keyIndex](auto& typedChannel)
            {
                if (keyIndex >= 0 && keyIndex < static_cast<int>(typedChannel.Keys.size()))
                {
                    typedChannel.Keys.erase(typedChannel.Keys.begin() + keyIndex);
                }
            };
            switch (channel)
            {
            case EChannel::Translation: erase(track.TranslationChannel); break;
            case EChannel::Rotation: erase(track.RotationChannel); break;
            case EChannel::Scale: erase(track.ScaleChannel); break;
            case EChannel::SunRotation: erase(track.SunRotationChannel); break;
            case EChannel::SunElevation: erase(track.SunElevationChannel); break;
            case EChannel::SkyRotation: erase(track.SkyRotationChannel); break;
            case EChannel::SunIntensity: erase(track.SunIntensityChannel); break;
            case EChannel::SkyIntensity: erase(track.SkyIntensityChannel); break;
            case EChannel::SunColor: erase(track.SunColorChannel); break;
            case EChannel::SkyColor: erase(track.SkyColorChannel); break;
            }
        }

        int DuplicateKeyAt(Assets::AnimationTrack& track, const EChannel channel, const int keyIndex,
                           const float time)
        {
            const auto duplicate = [keyIndex, time](auto& typedChannel)
            {
                if (keyIndex < 0 || keyIndex >= static_cast<int>(typedChannel.Keys.size())) return -1;
                return InsertKey(typedChannel, time, typedChannel.Keys[keyIndex].Value);
            };
            switch (channel)
            {
            case EChannel::Translation: return duplicate(track.TranslationChannel);
            case EChannel::Rotation: return duplicate(track.RotationChannel);
            case EChannel::Scale: return duplicate(track.ScaleChannel);
            case EChannel::SunRotation: return duplicate(track.SunRotationChannel);
            case EChannel::SunElevation: return duplicate(track.SunElevationChannel);
            case EChannel::SkyRotation: return duplicate(track.SkyRotationChannel);
            case EChannel::SunIntensity: return duplicate(track.SunIntensityChannel);
            case EChannel::SkyIntensity: return duplicate(track.SkyIntensityChannel);
            case EChannel::SunColor: return duplicate(track.SunColorChannel);
            case EChannel::SkyColor: return duplicate(track.SkyColorChannel);
            }
            return -1;
        }

        bool Contains(const std::vector<int>& values, const int value)
        {
            return std::ranges::find(values, value) != values.end();
        }

        void Toggle(std::vector<int>& values, const int value)
        {
            const auto found = std::ranges::find(values, value);
            if (found == values.end()) values.push_back(value);
            else values.erase(found);
        }

        bool IsSelected(const EditorUiState::SequencerState& state, const int track,
                        const EChannel channel, const int key)
        {
            return std::ranges::find(state.selectedKeys,
                EditorUiState::SequencerState::KeySelection{track, channel, key}) != state.selectedKeys.end();
        }

        void SelectOnly(EditorUiState::SequencerState& state, const int track,
                        const EChannel channel, const int key)
        {
            state.selectedTrack = track;
            state.selectedChannel = channel;
            state.selectedKey = key;
            state.selectedKeys.clear();
            if (key >= 0) state.selectedKeys.push_back({track, channel, key});
        }

        bool MatchesSearch(const Assets::AnimationTrack& track, const char* search)
        {
            if (!search || search[0] == '\0') return true;
            std::string haystack = track.NodeName_ + " " + track.AnimationName;
            std::string needle = search;
            std::ranges::transform(haystack, haystack.begin(), [](const unsigned char value)
            {
                return static_cast<char>(std::tolower(value));
            });
            std::ranges::transform(needle, needle.begin(), [](const unsigned char value)
            {
                return static_cast<char>(std::tolower(value));
            });
            return haystack.find(needle) != std::string::npos;
        }

        float TrackKeyDuration(const Assets::AnimationTrack& track)
        {
            float duration = 0.0f;
            for (const EChannel channel : ChannelsForTrack(track))
            {
                ForEachKeyTime(track, channel, [&duration](int, const float time)
                {
                    duration = std::max(duration, time);
                });
            }
            return duration;
        }

        float SequenceDuration(const std::vector<Assets::AnimationTrack>& tracks)
        {
            float duration = 1.0f;
            for (const auto& track : tracks)
            {
                duration = std::max(duration, std::max(track.Duration_, TrackKeyDuration(track)));
            }
            return duration;
        }

        class SequencerEditCommand final : public Runtime::Command::ICommand
        {
        public:
            SequencerEditCommand(Assets::Scene& scene, std::vector<Assets::AnimationTrack> before,
                                 std::vector<Assets::AnimationTrack> after, const float previewTime,
                                 std::string description)
                : scene_(scene), before_(std::move(before)), after_(std::move(after)),
                  previewTime_(previewTime), description_(std::move(description))
            {
            }

            bool Execute() override { return Apply(after_); }
            bool Undo() override { return Apply(before_); }
            std::string GetDescription() const override { return description_; }

        private:
            bool Apply(const std::vector<Assets::AnimationTrack>& tracks)
            {
                scene_.Tracks() = tracks;
                scene_.SetTracksPlaying(false);
                scene_.EvaluateTracks(previewTime_);
                return true;
            }

            Assets::Scene& scene_;
            std::vector<Assets::AnimationTrack> before_;
            std::vector<Assets::AnimationTrack> after_;
            float previewTime_ = 0.0f;
            std::string description_;
        };

        void CommitEdit(EditorContext& ctx, std::vector<Assets::AnimationTrack> before,
                        const float previewTime, const char* description)
        {
            ctx.engine.GetCommandHistory().Execute(std::make_unique<SequencerEditCommand>(
                ctx.scene, std::move(before), ctx.scene.Tracks(), previewTime, description));
            ctx.engine.SetProgressiveRendering(false);
        }

        float SnapTime(const EditorUiState::SequencerState& state, float time)
        {
            time = std::max(0.0f, time);
            if (state.snapToFrames && state.framesPerSecond > 0.0f)
            {
                time = std::round(time * state.framesPerSecond) / state.framesPerSecond;
            }
            return time;
        }

        float NiceTickStep(const float pixelsPerSecond)
        {
            const float rawStep = 80.0f / std::max(pixelsPerSecond, 1.0f);
            const float magnitude = std::pow(10.0f, std::floor(std::log10(rawStep)));
            const float normalized = rawStep / magnitude;
            const float nice = normalized <= 1.0f ? 1.0f : normalized <= 2.0f ? 2.0f : normalized <= 5.0f ? 5.0f : 10.0f;
            return nice * magnitude;
        }

        void DrawGrid(ImDrawList* drawList, const ImVec2& min, const ImVec2& max,
                      const float pixelsPerSecond, const float duration)
        {
            const float step = NiceTickStep(pixelsPerSecond);
            for (float time = 0.0f; time <= duration + step; time += step)
            {
                const float x = min.x + time * pixelsPerSecond;
                drawList->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), IM_COL32(90, 95, 105, 72));
            }
        }

        void DrawDiamond(ImDrawList* drawList, const ImVec2 center, const ImU32 color, const float radius)
        {
            const ImVec2 points[] = {
                ImVec2(center.x, center.y - radius), ImVec2(center.x + radius, center.y),
                ImVec2(center.x, center.y + radius), ImVec2(center.x - radius, center.y),
            };
            drawList->AddConvexPolyFilled(points, 4, color);
            drawList->AddPolyline(points, 4, IM_COL32(20, 22, 26, 230), ImDrawFlags_Closed, 1.0f);
        }

        void Preview(EditorContext& ctx, EditorUiState::SequencerState& state)
        {
            ctx.scene.SetTracksPlaying(false);
            ctx.scene.EvaluateTracks(state.currentTime);
            ctx.engine.SetProgressiveRendering(false);
        }

        float ClampKeyTime(const Assets::AnimationTrack& track, const EChannel channel,
                           const int keyIndex, float time)
        {
            constexpr float gap = 0.0001f;
            if (keyIndex > 0)
            {
                float previous = 0.0f;
                ForEachKeyTime(track, channel, [&](const int index, const float keyTime)
                {
                    if (index == keyIndex - 1) previous = keyTime;
                });
                time = std::max(time, previous + gap);
            }
            if (keyIndex + 1 < KeyCount(track, channel))
            {
                float next = time;
                ForEachKeyTime(track, channel, [&](const int index, const float keyTime)
                {
                    if (index == keyIndex + 1) next = keyTime;
                });
                time = std::min(time, next - gap);
            }
            return std::max(0.0f, time);
        }

        void SelectTimeRange(const std::vector<Assets::AnimationTrack>& tracks,
                             EditorUiState::SequencerState& state)
        {
            const float start = std::min(state.rangeStartTime, state.rangeEndTime);
            const float end = std::max(state.rangeStartTime, state.rangeEndTime);
            state.selectedKeys.clear();
            for (int trackIndex = 0; trackIndex < static_cast<int>(tracks.size()); ++trackIndex)
            {
                if (Contains(state.hiddenTracks, trackIndex)) continue;
                for (const EChannel channel : ChannelsForTrack(tracks[trackIndex]))
                {
                    ForEachKeyTime(tracks[trackIndex], channel, [&](const int keyIndex, const float keyTime)
                    {
                        if (keyTime >= start && keyTime <= end)
                        {
                            state.selectedKeys.push_back({trackIndex, channel, keyIndex});
                        }
                    });
                }
            }
            if (!state.selectedKeys.empty())
            {
                const auto& primary = state.selectedKeys.back();
                state.selectedTrack = primary.track;
                state.selectedChannel = primary.channel;
                state.selectedKey = primary.key;
            }
        }

        bool DrawTimelineRow(EditorContext& ctx, EditorUiState& ui, const int trackIndex,
                             const EChannel channel, const float timelineWidth, const float duration)
        {
            auto& state = ui.sequencer;
            auto& tracks = ctx.scene.Tracks();
            auto& track = tracks[trackIndex];
            const bool trackLocked = Contains(state.lockedTracks, trackIndex);

            ImGui::PushID(trackIndex * 32 + static_cast<int>(channel));
            ImGui::InvisibleButton("row", ImVec2(timelineWidth, kRowHeight));
            const bool hovered = ImGui::IsItemHovered();
            const bool active = ImGui::IsItemActive();
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(min, max, hovered ? IM_COL32(66, 72, 82, 85) : IM_COL32(35, 38, 44, 48));
            DrawGrid(drawList, min, max, state.pixelsPerSecond, duration);

            int hoveredKey = -1;
            float nearestDistance = kKeyRadius + 3.0f;
            ForEachKeyTime(track, channel, [&](const int keyIndex, const float keyTime)
            {
                const ImVec2 center(min.x + keyTime * state.pixelsPerSecond, min.y + kRowHeight * 0.5f);
                const float distance = std::abs(ImGui::GetIO().MousePos.x - center.x);
                if (hovered && distance < nearestDistance &&
                    std::abs(ImGui::GetIO().MousePos.y - center.y) < kKeyRadius + 4.0f)
                {
                    hoveredKey = keyIndex;
                    nearestDistance = distance;
                }
                const bool selected = IsSelected(state, trackIndex, channel, keyIndex) ||
                    (state.selectedTrack == trackIndex && state.selectedChannel == channel &&
                     state.selectedKey == keyIndex);
                DrawDiamond(drawList, center, ChannelColor(channel, selected), selected ? kKeyRadius + 1.5f : kKeyRadius);
            });

            if (state.rangeSelecting)
            {
                const float x0 = min.x + std::min(state.rangeStartTime, state.rangeEndTime) * state.pixelsPerSecond;
                const float x1 = min.x + std::max(state.rangeStartTime, state.rangeEndTime) * state.pixelsPerSecond;
                drawList->AddRectFilled(ImVec2(x0, min.y), ImVec2(x1, max.y), IM_COL32(55, 132, 255, 38));
                drawList->AddRect(ImVec2(x0, min.y), ImVec2(x1, max.y), IM_COL32(80, 155, 255, 180));
            }

            if (hoveredKey >= 0)
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            }

            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                state.selectedTrack = trackIndex;
                state.selectedChannel = channel;
                if (ImGui::GetIO().KeyShift && hoveredKey < 0)
                {
                    state.rangeSelecting = true;
                    state.rangeStartTime = SnapTime(state,
                        (ImGui::GetIO().MousePos.x - min.x) / state.pixelsPerSecond);
                    state.rangeEndTime = state.rangeStartTime;
                }
                else if (hoveredKey >= 0 && !trackLocked)
                {
                    state.editBefore = tracks;
                    state.duplicateOnDrag = ImGui::GetIO().KeyAlt;
                    if (state.duplicateOnDrag)
                    {
                        const float frame = 1.0f / std::max(state.framesPerSecond, 1.0f);
                        hoveredKey = DuplicateKeyAt(track, channel, hoveredKey,
                            SnapTime(state, *KeyTime(track, channel, hoveredKey) + frame));
                    }
                    SelectOnly(state, trackIndex, channel, hoveredKey);
                    state.dragOriginalTime = *KeyTime(track, channel, state.selectedKey);
                    state.draggingKey = true;
                }
                else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    if (trackLocked)
                    {
                        ImGui::PopID();
                        return false;
                    }
                    const auto before = tracks;
                    state.currentTime = SnapTime(state, (ImGui::GetIO().MousePos.x - min.x) / state.pixelsPerSecond);
                    state.selectedKey = AddKeyAt(ctx, track, channel, state.currentTime);
                    SelectOnly(state, trackIndex, channel, state.selectedKey);
                    track.Duration_ = std::max(track.Duration_, state.currentTime);
                    Preview(ctx, state);
                    CommitEdit(ctx, before, state.currentTime, "Add animation key");
                    ImGui::PopID();
                    return true;
                }
                else if (!ImGui::GetIO().KeyShift)
                {
                    SelectOnly(state, trackIndex, channel, -1);
                }
            }

            if (active && state.rangeSelecting)
            {
                state.rangeEndTime = SnapTime(state,
                    (ImGui::GetIO().MousePos.x - min.x) / state.pixelsPerSecond);
            }
            if (state.rangeSelecting && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                state.rangeSelecting = false;
                SelectTimeRange(tracks, state);
            }

            if (active && state.draggingKey && state.selectedTrack == trackIndex &&
                state.selectedChannel == channel && state.selectedKey >= 0)
            {
                float* keyTime = KeyTime(track, channel, state.selectedKey);
                if (keyTime)
                {
                    const float newTime = ClampKeyTime(track, channel, state.selectedKey,
                        SnapTime(state, (ImGui::GetIO().MousePos.x - min.x) / state.pixelsPerSecond));
                    if (std::abs(*keyTime - newTime) > 0.00001f)
                    {
                        *keyTime = newTime;
                        state.currentTime = newTime;
                        track.Duration_ = std::max(track.Duration_, newTime);
                        Preview(ctx, state);
                    }
                }
            }

            if (state.draggingKey && state.selectedTrack == trackIndex && state.selectedChannel == channel &&
                ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                state.draggingKey = false;
                const float* keyTime = KeyTime(track, channel, state.selectedKey);
                if (keyTime && (state.duplicateOnDrag ||
                    std::abs(*keyTime - state.dragOriginalTime) > 0.00001f))
                {
                    CommitEdit(ctx, std::move(state.editBefore), state.currentTime,
                        state.duplicateOnDrag ? "Duplicate animation key" : "Move animation key");
                    state.editBefore.clear();
                    state.duplicateOnDrag = false;
                    ImGui::PopID();
                    return true;
                }
                state.editBefore.clear();
                state.duplicateOnDrag = false;
            }

            ImGui::PopID();
            return false;
        }

        void DrawRuler(EditorUiState::SequencerState& state, const float timelineWidth, const float duration)
        {
            ImGui::InvisibleButton("ruler", ImVec2(timelineWidth, kRulerHeight));
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(min, max, IM_COL32(38, 41, 48, 255));
            const float step = NiceTickStep(state.pixelsPerSecond);
            for (float time = 0.0f; time <= duration + step; time += step)
            {
                const float x = min.x + time * state.pixelsPerSecond;
                drawList->AddLine(ImVec2(x, max.y - 9.0f), ImVec2(x, max.y), IM_COL32(155, 160, 172, 180));
                const std::string label = fmt::format("{:.2f}", time);
                drawList->AddText(ImVec2(x + 3.0f, min.y + 2.0f), IM_COL32(190, 194, 204, 220), label.c_str());
            }
            const float playheadX = min.x + state.currentTime * state.pixelsPerSecond;
            drawList->AddTriangleFilled(ImVec2(playheadX - 5.0f, min.y), ImVec2(playheadX + 5.0f, min.y),
                                        ImVec2(playheadX, min.y + 8.0f), IM_COL32(235, 78, 78, 255));
            drawList->AddLine(ImVec2(playheadX, min.y + 7.0f), ImVec2(playheadX, max.y), IM_COL32(235, 78, 78, 235), 1.5f);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                state.currentTime = SnapTime(state, (ImGui::GetIO().MousePos.x - min.x) / state.pixelsPerSecond);
            }
        }

        float FindAdjacentKeyTime(const std::vector<Assets::AnimationTrack>& tracks, const float currentTime,
                                  const bool next)
        {
            float result = next ? std::numeric_limits<float>::max() : -1.0f;
            for (const auto& track : tracks)
            {
                for (const EChannel channel : ChannelsForTrack(track))
                {
                    ForEachKeyTime(track, channel, [&](int, const float keyTime)
                    {
                        if (next && keyTime > currentTime + 0.0001f) result = std::min(result, keyTime);
                        if (!next && keyTime < currentTime - 0.0001f) result = std::max(result, keyTime);
                    });
                }
            }
            return result;
        }

        bool HasValidKeySelection(const std::vector<Assets::AnimationTrack>& tracks,
                                  const EditorUiState::SequencerState& state)
        {
            return state.selectedTrack >= 0 && state.selectedTrack < static_cast<int>(tracks.size()) &&
                state.selectedKey >= 0 &&
                state.selectedKey < KeyCount(tracks[state.selectedTrack], state.selectedChannel);
        }

        void TogglePlayback(EditorContext& ctx, EditorUiState::SequencerState& state, const float duration)
        {
            auto& tracks = ctx.scene.Tracks();
            const bool playing = std::ranges::any_of(tracks, [](const auto& track) { return track.Playing(); });
            if (playing)
            {
                ctx.scene.SetTracksPlaying(false);
                return;
            }
            if (state.currentTime >= duration) state.currentTime = 0.0f;
            ctx.scene.EvaluateTracks(state.currentTime);
            ctx.scene.SetTracksPlaying(true);
        }

        void AddSelectedKey(EditorContext& ctx, EditorUiState::SequencerState& state)
        {
            auto& tracks = ctx.scene.Tracks();
            if (state.selectedTrack < 0 || state.selectedTrack >= static_cast<int>(tracks.size()) ||
                Contains(state.lockedTracks, state.selectedTrack)) return;
            const auto before = tracks;
            auto& track = tracks[state.selectedTrack];
            state.selectedKey = AddKeyAt(ctx, track, state.selectedChannel, state.currentTime);
            SelectOnly(state, state.selectedTrack, state.selectedChannel, state.selectedKey);
            track.Duration_ = std::max(track.Duration_, state.currentTime);
            Preview(ctx, state);
            CommitEdit(ctx, before, state.currentTime, "Add animation key");
        }

        void DeleteSelectedKeys(EditorContext& ctx, EditorUiState::SequencerState& state)
        {
            auto& tracks = ctx.scene.Tracks();
            if (!HasValidKeySelection(tracks, state)) return;
            auto selections = state.selectedKeys;
            if (selections.empty()) selections.push_back({state.selectedTrack, state.selectedChannel, state.selectedKey});
            std::ranges::sort(selections, [](const auto& left, const auto& right)
            {
                if (left.track != right.track) return left.track > right.track;
                if (left.channel != right.channel) return left.channel > right.channel;
                return left.key > right.key;
            });
            const auto before = tracks;
            bool deleted = false;
            for (const auto& selection : selections)
            {
                if (selection.track < 0 || selection.track >= static_cast<int>(tracks.size()) ||
                    Contains(state.lockedTracks, selection.track)) continue;
                EraseKey(tracks[selection.track], selection.channel, selection.key);
                deleted = true;
            }
            if (!deleted) return;
            state.selectedKey = -1;
            state.selectedKeys.clear();
            Preview(ctx, state);
            CommitEdit(ctx, before, state.currentTime, "Delete animation keys");
        }

        void DuplicateSelectedKey(EditorContext& ctx, EditorUiState::SequencerState& state)
        {
            auto& tracks = ctx.scene.Tracks();
            if (!HasValidKeySelection(tracks, state) || Contains(state.lockedTracks, state.selectedTrack)) return;
            auto& track = tracks[state.selectedTrack];
            const auto before = tracks;
            const float* sourceTime = KeyTime(track, state.selectedChannel, state.selectedKey);
            const float frame = 1.0f / std::max(state.framesPerSecond, 1.0f);
            const float duplicateTime = SnapTime(state, (sourceTime ? *sourceTime : state.currentTime) + frame);
            state.selectedKey = DuplicateKeyAt(track, state.selectedChannel, state.selectedKey, duplicateTime);
            SelectOnly(state, state.selectedTrack, state.selectedChannel, state.selectedKey);
            state.currentTime = duplicateTime;
            track.Duration_ = std::max(track.Duration_, duplicateTime);
            Preview(ctx, state);
            CommitEdit(ctx, before, state.currentTime, "Duplicate animation key");
        }

        void HandleShortcuts(EditorContext& ctx, EditorUiState::SequencerState& state, const float duration)
        {
            const ImGuiIO& io = ImGui::GetIO();
            if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || io.WantTextInput) return;
            const bool command = io.KeyCtrl || io.KeySuper;
            if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) TogglePlayback(ctx, state, duration);
            if (!command && ImGui::IsKeyPressed(ImGuiKey_K, false)) AddSelectedKey(ctx, state);
            if (!command && ImGui::IsKeyPressed(ImGuiKey_S, false)) state.snapToFrames = !state.snapToFrames;
            if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) DeleteSelectedKeys(ctx, state);
            if (command && ImGui::IsKeyPressed(ImGuiKey_D, false)) DuplicateSelectedKey(ctx, state);
            if (ImGui::IsKeyPressed(ImGuiKey_Home, false))
            {
                state.currentTime = 0.0f;
                Preview(ctx, state);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_End, false))
            {
                state.currentTime = duration;
                Preview(ctx, state);
            }

            auto& tracks = ctx.scene.Tracks();
            if (!command && ImGui::IsKeyPressed(ImGuiKey_G, false) &&
                HasValidKeySelection(tracks, state) && !Contains(state.lockedTracks, state.selectedTrack))
            {
                state.keyboardGrab = true;
                state.keyboardGrabMouseX = io.MousePos.x;
                state.keyboardGrabOriginalTime = *KeyTime(
                    tracks[state.selectedTrack], state.selectedChannel, state.selectedKey);
                state.editBefore = tracks;
            }
            if (!state.keyboardGrab) return;

            float* time = KeyTime(tracks[state.selectedTrack], state.selectedChannel, state.selectedKey);
            if (time)
            {
                *time = ClampKeyTime(tracks[state.selectedTrack], state.selectedChannel, state.selectedKey,
                    SnapTime(state, state.keyboardGrabOriginalTime +
                        (io.MousePos.x - state.keyboardGrabMouseX) / state.pixelsPerSecond));
                state.currentTime = *time;
                Preview(ctx, state);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            {
                tracks = state.editBefore;
                state.keyboardGrab = false;
                state.editBefore.clear();
                Preview(ctx, state);
            }
            else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsKeyPressed(ImGuiKey_Enter, false))
            {
                state.keyboardGrab = false;
                CommitEdit(ctx, std::move(state.editBefore), state.currentTime, "Move animation key");
                state.editBefore.clear();
            }
        }

        void DrawToolbar(EditorContext& ctx, EditorUiState& ui, const float duration)
        {
            auto& state = ui.sequencer;
            auto& tracks = ctx.scene.Tracks();
            const bool playing = std::ranges::any_of(tracks, [](const auto& track) { return track.Playing(); });

            if (ImGui::Button(ICON_FA_BACKWARD_FAST))
            {
                state.currentTime = 0.0f;
                Preview(ctx, state);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Start (Home)");
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_BACKWARD_STEP))
            {
                const float time = FindAdjacentKeyTime(tracks, state.currentTime, false);
                state.currentTime = time >= 0.0f ? time : 0.0f;
                Preview(ctx, state);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Previous key");
            ImGui::SameLine();
            if (ImGui::Button(playing ? ICON_FA_PAUSE : ICON_FA_PLAY))
            {
                TogglePlayback(ctx, state, duration);
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_STOP))
            {
                state.currentTime = 0.0f;
                Preview(ctx, state);
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_FORWARD_STEP))
            {
                const float time = FindAdjacentKeyTime(tracks, state.currentTime, true);
                state.currentTime = time == std::numeric_limits<float>::max() ? duration : time;
                Preview(ctx, state);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Next key");
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_FORWARD_FAST))
            {
                state.currentTime = duration;
                Preview(ctx, state);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("End (End)");

            ImGui::SameLine(0.0f, 12.0f);
            ImGui::SetNextItemWidth(100.0f);
            float editedTime = state.currentTime;
            if (ImGui::DragFloat("##SequenceTime", &editedTime, 0.01f, 0.0f, duration, "%.3f s"))
            {
                state.currentTime = SnapTime(state, editedTime);
                Preview(ctx, state);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("/ %.2f s", duration);

            ImGui::SameLine(0.0f, 16.0f);
            ImGui::Checkbox("Snap", &state.snapToFrames);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(58.0f);
            ImGui::DragFloat("FPS", &state.framesPerSecond, 1.0f, 1.0f, 240.0f, "%.0f");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            ImGui::SliderFloat("Zoom", &state.pixelsPerSecond, 35.0f, 400.0f, "%.0f px/s");

            ImGui::SameLine(0.0f, 16.0f);
            Assets::Node* selectedNode = ctx.scene.GetNodeByInstanceId(ctx.scene.GetSelectedId());
            const bool canAddNodeTrack = selectedNode != nullptr && !selectedNode->IsSceneReferenceInternal();
            ImGui::BeginDisabled(!canAddNodeTrack);
            if (ImGui::Button(ICON_FA_PLUS " Track"))
            {
                const auto existing = std::ranges::find_if(tracks, [selectedNode](const Assets::AnimationTrack& track)
                {
                    return track.Target_ == Assets::AnimationTrack::Target::NodeTransform &&
                        track.NodeName_ == selectedNode->GetName();
                });
                if (existing != tracks.end())
                {
                    state.selectedTrack = static_cast<int>(existing - tracks.begin());
                }
                else
                {
                    const auto before = tracks;
                    Assets::AnimationTrack track;
                    track.AnimationName = "Sequence";
                    track.NodeName_ = selectedNode->GetName();
                    track.Duration_ = duration;
                    tracks.push_back(std::move(track));
                    state.selectedTrack = static_cast<int>(tracks.size()) - 1;
                    state.selectedChannel = EChannel::Translation;
                    state.selectedKey = -1;
                    CommitEdit(ctx, before, state.currentTime, "Add animation track");
                }
                state.selectedChannel = EChannel::Translation;
                state.selectedKey = -1;
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                if (!selectedNode)
                {
                    ImGui::SetTooltip("Select a scene node first");
                }
                else if (selectedNode->IsSceneReferenceInternal())
                {
                    ImGui::SetTooltip("Animation tracks cannot be authored on scene-reference internal nodes");
                }
                else
                {
                    ImGui::SetTooltip("Select this node's existing track, or create one if missing");
                }
            }
            ImGui::SameLine(0.0f, 2.0f);
            if (ImGui::ArrowButton("##AddTrackOptions", ImGuiDir_Down))
            {
                ImGui::OpenPopup("AddTrackOptionsPopup");
            }
            if (ImGui::BeginPopup("AddTrackOptionsPopup"))
            {
                if (ImGui::MenuItem("Environment Track"))
                {
                    const auto existing = std::ranges::find_if(tracks, [](const Assets::AnimationTrack& track)
                    {
                        return track.Target_ == Assets::AnimationTrack::Target::Environment;
                    });
                    if (existing != tracks.end())
                    {
                        state.selectedTrack = static_cast<int>(existing - tracks.begin());
                    }
                    else
                    {
                        const auto before = tracks;
                        Assets::AnimationTrack track;
                        track.AnimationName = "Environment";
                        track.NodeName_ = "Environment";
                        track.Target_ = Assets::AnimationTrack::Target::Environment;
                        track.Duration_ = duration;
                        tracks.push_back(std::move(track));
                        state.selectedTrack = static_cast<int>(tracks.size()) - 1;
                        CommitEdit(ctx, before, state.currentTime, "Add environment track");
                    }
                    state.selectedChannel = EChannel::SunRotation;
                    state.selectedKey = -1;
                }
                ImGui::EndPopup();
            }

            const bool canAddKey = state.selectedTrack >= 0 && state.selectedTrack < static_cast<int>(tracks.size()) &&
                !Contains(state.lockedTracks, state.selectedTrack);
            ImGui::SameLine();
            ImGui::BeginDisabled(!canAddKey);
            if (ImGui::Button(ICON_FA_DIAMOND " Key"))
            {
                AddSelectedKey(ctx, state);
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            ImGui::BeginDisabled(!HasValidKeySelection(tracks, state));
            if (ImGui::Button(ICON_FA_TRASH " Key"))
            {
                DeleteSelectedKeys(ctx, state);
            }
            ImGui::EndDisabled();

            ImGui::SameLine(0.0f, 12.0f);
            ImGui::SetNextItemWidth(180.0f);
            ImGui::InputTextWithHint("##SequencerSearch", ICON_FA_MAGNIFYING_GLASS "  Search tracks...",
                                     state.search, std::size(state.search));
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_KEYBOARD "##SequencerShortcuts"))
            {
                ImGui::OpenPopup("SequencerShortcutsPopup");
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Keyboard shortcuts");
            if (ImGui::BeginPopup("SequencerShortcutsPopup"))
            {
                ImGui::SeparatorText("Sequencer Shortcuts");
                ImGui::Text("Space    Play / Pause");
                ImGui::Text("K        Add key at playhead");
                ImGui::Text("S        Toggle frame snapping");
                ImGui::Text("Delete   Delete selected keys");
                ImGui::Text("Ctrl+D   Duplicate selected key");
                ImGui::Text("G        Grab key (click to confirm, Esc to cancel)");
                ImGui::Text("Ctrl+Wheel  Zoom timeline");
                ImGui::Text("Shift+Drag  Select a time range");
                ImGui::Text("Alt+Drag    Duplicate and move key");
                ImGui::Text("Home / End  Jump to sequence bounds");
                ImGui::EndPopup();
            }
        }

        void BeginValueEdit(EditorUiState::SequencerState& state,
                            std::vector<Assets::AnimationTrack> beforeCandidate)
        {
            if (ImGui::IsItemActivated())
            {
                state.editBefore = std::move(beforeCandidate);
                state.trackingValueEdit = true;
            }
        }

        void FinishValueEdit(EditorContext& ctx, EditorUiState::SequencerState& state, const char* description)
        {
            if (state.trackingValueEdit && ImGui::IsItemDeactivatedAfterEdit())
            {
                CommitEdit(ctx, std::move(state.editBefore), state.currentTime, description);
                state.editBefore.clear();
                state.trackingValueEdit = false;
            }
        }

        void DrawKeyInspector(EditorContext& ctx, EditorUiState& ui)
        {
            auto& tracks = ctx.scene.Tracks();
            auto& state = ui.sequencer;
            ImGui::SeparatorText("KEY PROPERTIES");
            if (state.selectedTrack < 0 || state.selectedTrack >= static_cast<int>(tracks.size()))
            {
                ImGui::TextDisabled("Select a track or key to inspect it.");
                return;
            }

            auto& track = tracks[state.selectedTrack];
            const bool locked = Contains(state.lockedTracks, state.selectedTrack);
            ImGui::TextDisabled("Selected track");
            ImGui::TextWrapped("%s / %s", track.Target_ == Assets::AnimationTrack::Target::Environment
                ? "Environment" : track.NodeName_.c_str(), ChannelLabel(state.selectedChannel));
            ImGui::Spacing();
            ImGui::BeginDisabled(locked);
            const auto beforeDuration = tracks;
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::DragFloat("##TrackDuration", &track.Duration_, 0.01f, TrackKeyDuration(track), 3600.0f,
                             "Duration  %.3f s");
            BeginValueEdit(state, beforeDuration);
            FinishValueEdit(ctx, state, "Edit animation track duration");
            const auto beforeSpeed = tracks;
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::DragFloat("##TrackSpeed", &track.PlaySpeed_, 0.01f, 0.01f, 16.0f, "Speed  %.2fx");
            BeginValueEdit(state, beforeSpeed);
            FinishValueEdit(ctx, state, "Edit animation track speed");
            ImGui::EndDisabled();
            if (locked) ImGui::TextColored(ImVec4(1.0f, 0.67f, 0.25f, 1.0f), ICON_FA_LOCK " Track is locked");

            if (!HasValidKeySelection(tracks, state))
            {
                ImGui::Spacing();
                ImGui::TextWrapped("Double-click the timeline or press K to create a key at the playhead.");
                ImGui::Spacing();
                ImGui::BeginDisabled(locked);
                if (ImGui::Button(ICON_FA_TRASH "  Delete Track", ImVec2(-1.0f, 0.0f)))
                {
                    const auto before = tracks;
                    tracks.erase(tracks.begin() + state.selectedTrack);
                    state.selectedTrack = -1;
                    state.selectedKey = -1;
                    state.selectedKeys.clear();
                    state.lockedTracks.clear();
                    state.hiddenTracks.clear();
                    CommitEdit(ctx, before, state.currentTime, "Delete animation track");
                }
                ImGui::EndDisabled();
                return;
            }

            if (state.selectedKeys.size() > 1)
            {
                ImGui::TextColored(ImVec4(0.35f, 0.68f, 1.0f, 1.0f), "%zu keys selected", state.selectedKeys.size());
            }

            ImGui::BeginDisabled(locked);

            float* time = KeyTime(track, state.selectedChannel, state.selectedKey);
            if (time)
            {
                const auto before = tracks;
                float edited = *time;
                ImGui::SetNextItemWidth(130.0f);
                if (ImGui::DragFloat("Time", &edited, 0.01f, 0.0f, std::max(track.Duration_, 0.0f), "%.3f s"))
                {
                    *time = ClampKeyTime(track, state.selectedChannel, state.selectedKey, SnapTime(state, edited));
                    state.currentTime = *time;
                    Preview(ctx, state);
                }
                BeginValueEdit(state, before);
                FinishValueEdit(ctx, state, "Edit animation key time");
            }

            const auto drawVec3 = [&](auto& typedChannel, const char* label, const bool color)
            {
                auto& value = typedChannel.Keys[state.selectedKey].Value;
                const auto before = tracks;
                const bool changed = color
                    ? ImGui::ColorEdit3(label, &value.x, ImGuiColorEditFlags_Float)
                    : ImGui::DragFloat3(label, &value.x, 0.01f);
                if (changed) Preview(ctx, state);
                BeginValueEdit(state, before);
                FinishValueEdit(ctx, state, "Edit animation key value");
            };
            const auto drawFloat = [&](auto& typedChannel, const char* label)
            {
                auto& value = typedChannel.Keys[state.selectedKey].Value;
                const auto before = tracks;
                if (ImGui::DragFloat(label, &value, 0.01f)) Preview(ctx, state);
                BeginValueEdit(state, before);
                FinishValueEdit(ctx, state, "Edit animation key value");
            };

            switch (state.selectedChannel)
            {
            case EChannel::Translation: drawVec3(track.TranslationChannel, "Value", false); break;
            case EChannel::Rotation:
            {
                auto& value = track.RotationChannel.Keys[state.selectedKey].Value;
                const auto before = tracks;
                glm::vec4 xyzw(value.x, value.y, value.z, value.w);
                if (ImGui::DragFloat4("Quaternion", &xyzw.x, 0.005f, -1.0f, 1.0f))
                {
                    value = glm::normalize(glm::quat(xyzw.w, xyzw.x, xyzw.y, xyzw.z));
                    Preview(ctx, state);
                }
                BeginValueEdit(state, before);
                FinishValueEdit(ctx, state, "Edit rotation key");
                break;
            }
            case EChannel::Scale: drawVec3(track.ScaleChannel, "Value", false); break;
            case EChannel::SunRotation: drawFloat(track.SunRotationChannel, "Value"); break;
            case EChannel::SunElevation: drawFloat(track.SunElevationChannel, "Value"); break;
            case EChannel::SkyRotation: drawFloat(track.SkyRotationChannel, "Value"); break;
            case EChannel::SunIntensity: drawFloat(track.SunIntensityChannel, "Value"); break;
            case EChannel::SkyIntensity: drawFloat(track.SkyIntensityChannel, "Value"); break;
            case EChannel::SunColor: drawVec3(track.SunColorChannel, "Value", true); break;
            case EChannel::SkyColor: drawVec3(track.SkyColorChannel, "Value", true); break;
            }
            ImGui::SeparatorText("EDIT");
            if (ImGui::Button(ICON_FA_COPY "  Duplicate", ImVec2(-1.0f, 0.0f)))
            {
                DuplicateSelectedKey(ctx, state);
            }
            if (ImGui::Button(ICON_FA_TRASH "  Delete", ImVec2(-1.0f, 0.0f)))
            {
                DeleteSelectedKeys(ctx, state);
            }
            ImGui::EndDisabled();
        }
    } // namespace

    void DrawSequencerPanel(EditorContext& ctx, EditorUiState& ui)
    {
        if (!ImGui::Begin("Sequencer", &ui.sequencerPanel, ImGuiWindowFlags_NoCollapse))
        {
            ImGui::End();
            return;
        }

        auto& tracks = ctx.scene.Tracks();
        auto& state = ui.sequencer;
        if (!state.initialized)
        {
            state.currentTime = tracks.empty() ? 0.0f : tracks.front().Time_;
            state.initialized = true;
        }
        if (state.selectedTrack >= static_cast<int>(tracks.size()))
        {
            state.selectedTrack = -1;
            state.selectedKey = -1;
        }

        const bool playing = std::ranges::any_of(tracks, [](const auto& track) { return track.Playing(); });
        if (playing && !tracks.empty())
        {
            state.currentTime = std::max(0.0f, tracks.front().Time_);
        }

        const float duration = SequenceDuration(tracks);
        if (!playing)
        {
            state.currentTime = std::clamp(state.currentTime, 0.0f, duration);
        }
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
            (ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper) && ImGui::GetIO().MouseWheel != 0.0f)
        {
            state.pixelsPerSecond = std::clamp(state.pixelsPerSecond *
                std::pow(1.12f, ImGui::GetIO().MouseWheel), 35.0f, 600.0f);
        }
        HandleShortcuts(ctx, state, duration);
        DrawToolbar(ctx, ui, duration);
        if (state.keyboardGrab)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.35f, 0.68f, 1.0f, 1.0f),
                               "Moving key - click/Enter to confirm, Esc to cancel");
        }
        ImGui::Separator();

        if (tracks.empty())
        {
            const float available = ImGui::GetContentRegionAvail().y;
            ImGui::Dummy(ImVec2(0.0f, std::max(20.0f, available * 0.28f)));
            const char* message = "No animation tracks in this scene";
            const float width = ImGui::CalcTextSize(message).x;
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), (ImGui::GetWindowWidth() - width) * 0.5f));
            ImGui::TextDisabled("%s", message);
            ImGui::TextDisabled("Select a scene node and use + Track to begin.");
            ImGui::End();
            return;
        }

        const float workAreaWidth = std::max(320.0f, ImGui::GetContentRegionAvail().x - kInspectorColumnWidth - 8.0f);
        const float timelineWidth = std::max(duration * state.pixelsPerSecond + 80.0f,
                                             workAreaWidth - kTrackColumnWidth);
        const float tableHeight = std::max(150.0f, ImGui::GetContentRegionAvail().y);
        constexpr ImGuiTableFlags layoutFlags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
        if (!ImGui::BeginTable("SequencerLayout", 2, layoutFlags, ImVec2(0.0f, tableHeight)))
        {
            ImGui::End();
            return;
        }
        ImGui::TableSetupColumn("Workspace", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Key Properties", ImGuiTableColumnFlags_WidthFixed, kInspectorColumnWidth);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersInnerH |
            ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit |
            ImGuiTableFlags_RowBg;
        if (ImGui::BeginTable("SequencerDopeSheet", 2, tableFlags, ImVec2(0.0f, tableHeight)))
        {
            ImGui::TableSetupColumn("Tracks", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide,
                                    kTrackColumnWidth);
            ImGui::TableSetupColumn("Timeline", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide,
                                    timelineWidth);
            ImGui::TableSetupScrollFreeze(1, 1);

            ImGui::TableNextRow(ImGuiTableRowFlags_Headers, kRulerHeight);
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("TRACKS / CHANNELS");
            ImGui::TableSetColumnIndex(1);
            const float oldTime = state.currentTime;
            DrawRuler(state, timelineWidth, duration);
            if (std::abs(oldTime - state.currentTime) > 0.00001f) Preview(ctx, state);

            bool timelineMutated = false;
            for (int trackIndex = 0; trackIndex < static_cast<int>(tracks.size()) && !timelineMutated; ++trackIndex)
            {
                auto& track = tracks[trackIndex];
                if (!MatchesSearch(track, state.search)) continue;
                const bool hidden = Contains(state.hiddenTracks, trackIndex);
                const bool locked = Contains(state.lockedTracks, trackIndex);
                ImGui::TableNextRow(ImGuiTableRowFlags_None, kRowHeight);
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(trackIndex);
                const std::string targetLabel = track.Target_ == Assets::AnimationTrack::Target::Environment
                    ? "Environment"
                    : track.NodeName_;
                const std::string label = fmt::format("{}  {}  [{}]##track",
                    track.Target_ == Assets::AnimationTrack::Target::Environment ? ICON_FA_SUN : ICON_FA_CUBE,
                    targetLabel, track.AnimationName.empty() ? "Sequence" : track.AnimationName);
                const bool open = ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen |
                    (state.selectedTrack == trackIndex ? ImGuiTreeNodeFlags_Selected : 0));
                if (ImGui::IsItemClicked())
                {
                    state.selectedTrack = trackIndex;
                    state.selectedKey = -1;
                    state.selectedKeys.clear();
                }
                ImGui::SameLine(kTrackColumnWidth - 58.0f);
                if (ImGui::SmallButton(hidden ? ICON_FA_EYE_SLASH : ICON_FA_EYE))
                {
                    Toggle(state.hiddenTracks, trackIndex);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(hidden ? "Show track keys" : "Hide track keys");
                ImGui::SameLine(0.0f, 3.0f);
                if (ImGui::SmallButton(locked ? ICON_FA_LOCK : ICON_FA_LOCK_OPEN))
                {
                    Toggle(state.lockedTracks, trackIndex);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(locked ? "Unlock track editing" : "Lock track editing");
                ImGui::PopID();

                ImGui::TableSetColumnIndex(1);
                ImGui::InvisibleButton(fmt::format("##trackSummary{}", trackIndex).c_str(), ImVec2(timelineWidth, kRowHeight));
                const ImVec2 rowMin = ImGui::GetItemRectMin();
                const ImVec2 rowMax = ImGui::GetItemRectMax();
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                drawList->AddRectFilled(rowMin, rowMax, IM_COL32(46, 50, 58, 130));
                DrawGrid(drawList, rowMin, rowMax, state.pixelsPerSecond, duration);
                if (!hidden) for (const EChannel channel : ChannelsForTrack(track))
                {
                    ForEachKeyTime(track, channel, [&](int, const float keyTime)
                    {
                        DrawDiamond(drawList, ImVec2(rowMin.x + keyTime * state.pixelsPerSecond,
                                                    rowMin.y + kRowHeight * 0.5f),
                                    ChannelColor(channel, false), 3.0f);
                    });
                }

                if (!open || hidden) continue;
                for (const EChannel channel : ChannelsForTrack(track))
                {
                    ImGui::TableNextRow(ImGuiTableRowFlags_None, kRowHeight);
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Indent(18.0f);
                    ImGui::PushID(trackIndex * 32 + static_cast<int>(channel));
                    const bool selected = state.selectedTrack == trackIndex && state.selectedChannel == channel;
                    const std::string channelLabel = fmt::format("{}  {}", ChannelLabel(channel), KeyCount(track, channel));
                    if (ImGui::Selectable(channelLabel.c_str(), selected, 0, ImVec2(0.0f, kRowHeight)))
                    {
                        state.selectedTrack = trackIndex;
                        state.selectedChannel = channel;
                        state.selectedKey = -1;
                    }
                    ImGui::PopID();
                    ImGui::Unindent(18.0f);
                    ImGui::TableSetColumnIndex(1);
                    timelineMutated = DrawTimelineRow(ctx, ui, trackIndex, channel, timelineWidth, duration);
                    if (timelineMutated) break;
                }
                if (open) ImGui::TreePop();
            }
            ImGui::EndTable();
        }

        ImGui::TableSetColumnIndex(1);
        DrawKeyInspector(ctx, ui);
        ImGui::EndTable();
        ImGui::End();
    }
} // namespace Editor
