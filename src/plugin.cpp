#include <D2RLPlugin/api.h>
#include <D2RLPlugin/logging.h>

#include "resource_ids.h"
#include "sort_panel_layout.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {

constexpr std::uintptr_t CharacterSelectEventRva = 0x61FDF0;
constexpr std::uintptr_t CharacterSelectModeSelectorOffset = 0x170;
constexpr std::uintptr_t CharacterSelectListOffset = 0x178;
constexpr std::uintptr_t ModeSelectedIndexOffset = 0x16C4;
constexpr std::uintptr_t ListSelectedIndexOffset = 0x248;
constexpr std::uintptr_t ListVectorOffset = 0xD8;
constexpr std::uintptr_t VectorDataOffset = 0x58;
constexpr std::uintptr_t VectorSizeOffset = 0x60;
constexpr std::uintptr_t WidgetChildrenOffset = 0x58;
constexpr std::uintptr_t WidgetChildCountOffset = 0x60;
constexpr std::uintptr_t WidgetNameOffset = 0x08;
constexpr std::uintptr_t WidgetParentOffset = 0x30;
constexpr std::uintptr_t WidgetVisibleFlagOffset = 0x51;
constexpr std::uintptr_t RowPositionOffset = 0x70;
constexpr std::uintptr_t RowSizeOffset = 0x78;
constexpr std::uintptr_t TextWidgetValueOffset = 0x88;
constexpr std::uintptr_t RowWidgetVtableRva = 0x1D78108;
constexpr std::uintptr_t TextWidgetVtableRva = 0x1CF4610;
constexpr std::int32_t OfflineModeIndex = 1;
constexpr std::int32_t MaximumCharacterRows = 32;
constexpr std::size_t MaximumNameLength = 32;
constexpr std::size_t MaximumMetadataLength = 128;
constexpr std::size_t MaximumConfigBytes = 65536;

constexpr std::array<std::uint8_t, 32> CharacterSelectEventExpected{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x80, 0x7A,
    0x04, 0x00, 0x48, 0x8B, 0xDA, 0x74, 0x04, 0x8B,
    0x02, 0xEB, 0x0D, 0x48, 0x8B, 0x81, 0x70, 0x01,
    0x00, 0x00, 0x8B, 0x80, 0xC4, 0x16, 0x00, 0x00,
};

constexpr char DefaultConfig[] = R"toml(# Offline Character Order
# Only the Offline character selection screen is changed.
# Use the arrows at the top of Offline character select to change the mode immediately, or edit this file and restart D2RLoader.

config_version = 1
sort_mode = "name_ascending" # most_recent, level_descending, level_ascending, name_ascending, name_descending, class_level_descending, or custom
custom_order = [] # Custom mode order; Ctrl+W/S moves the selected character. Unlisted names follow the game's original order.
)toml";

enum class SortMode : std::uint8_t {
    Native,
    MostRecent,
    NameAscending,
    NameDescending,
    LevelAscending,
    LevelDescending,
    ClassLevelDescending,
    Custom,
};

constexpr std::array<SortMode, 7> SortModeCycle{
    SortMode::MostRecent,
    SortMode::LevelDescending,
    SortMode::LevelAscending,
    SortMode::NameAscending,
    SortMode::NameDescending,
    SortMode::ClassLevelDescending,
    SortMode::Custom,
};

constexpr std::array<std::string_view, 7> SortModeLabelWidgets{
    "MostRecentLabel",
    "LevelDescendingLabel",
    "LevelAscendingLabel",
    "NameAscendingLabel",
    "NameDescendingLabel",
    "ClassOrderLabel",
    "CustomLabel",
};

auto SortModeLabelIndex(SortMode mode) noexcept -> std::size_t {
    switch (mode) {
    case SortMode::MostRecent:
    case SortMode::Native: return 0;
    case SortMode::LevelDescending: return 1;
    case SortMode::LevelAscending: return 2;
    case SortMode::NameAscending: return 3;
    case SortMode::NameDescending: return 4;
    case SortMode::ClassLevelDescending: return 5;
    case SortMode::Custom: return 6;
    }
    return 0;
}

auto CycleSortMode(SortMode current, int direction) noexcept -> SortMode {
    int index = -1;
    for (std::size_t candidate = 0; candidate < SortModeCycle.size(); ++candidate) {
        if ((current == SortMode::Native && candidate == 0)
                || SortModeCycle[candidate] == current) {
            index = static_cast<int>(candidate);
            break;
        }
    }
    if (index < 0) index = direction > 0 ? -1 : 0;
    const auto count = static_cast<int>(SortModeCycle.size());
    const auto next = (index + direction + count) % count;
    return SortModeCycle[static_cast<std::size_t>(next)];
}

struct CharacterRow {
    void* widget{};
    std::string name{};
    std::int32_t level{-1};
    std::int32_t classOrder{-1};
    std::uint32_t x{};
    std::uint32_t y{};
    std::uint32_t width{};
    std::uint32_t height{};
};

struct RowPosition {
    std::uint32_t x{};
    std::uint32_t y{};
};

struct DatasetRowSignature {
    std::uintptr_t widget{};
    std::string name{};
    std::int32_t level{};
    std::int32_t classOrder{};

    bool operator==(const DatasetRowSignature&) const = default;
};

struct WidgetNode {
    void* widget{};
    std::uint32_t depth{};
};

using CharacterSelectEventFn = void*(__fastcall*)(void* panel, void* event) noexcept;

const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
CharacterSelectEventFn OriginalCharacterSelectEvent{};
SortMode CurrentMode{SortMode::NameAscending};
std::atomic_bool CustomModeActive{};
std::vector<std::string> CustomOrderNames{};
std::vector<std::string> NativeOrderNames{};
std::vector<DatasetRowSignature> DatasetSignature{};
std::vector<RowPosition> SlotPositions{};
void* CachedListWidget{};
void* ActiveOfflinePanelWidget{};
const D2RL::ThreadServiceV1* UiThreadService{};
const D2RL::ResourceServiceV1* SortPanelResourceService{};
const D2RL::PanelServiceV1* SortPanelService{};
const D2RL::SharedEventServiceV1* SortPanelEventService{};
const D2RL::LifecycleServiceV1* GameplayLifecycleService{};
const D2RL::WidgetServiceV1* SortPanelWidgetService{};
D2RL::Resources::RegistrationHandle SortPanelLayoutResourceHandle{
    D2RL::Resources::InvalidHandle};
std::array<D2RL::Resources::RegistrationHandle, 3> SortPanelArtworkHandles{
    D2RL::Resources::InvalidHandle,
    D2RL::Resources::InvalidHandle,
    D2RL::Resources::InvalidHandle,
};
D2RL::Panels::RegistrationHandle SortPanelRegistrationHandle{
    D2RL::Panels::InvalidHandle};
D2RL::SharedEvents::ListenerHandle SortPanelMessageHandle{
    D2RL::SharedEvents::InvalidHandle};
D2RL::Lifecycle::ListenerHandle GameJoinedListenerHandle{
    D2RL::Lifecycle::InvalidHandle};
struct CustomOrderInputAction {
    std::int32_t direction{};
    std::atomic_bool pressed{};
    std::atomic_bool consumed{};
};
std::array<CustomOrderInputAction, 2> CustomOrderInputActions{{
    {.direction = -1},
    {.direction = 1},
}};
std::atomic<HHOOK> CustomOrderKeyboardHook{};
bool DatasetKnown{};
bool SelectionOffsetKnown{};
std::int32_t SelectionOffsetY{};
void* LastSyncedSelectionWidget{};
void* SelectedCharacterWidget{};
std::string SelectedCharacterName{};
std::int32_t LastObservedNativeSelectedIndex{-2};
std::atomic_bool Operational{};
std::atomic_bool OfflineSelectionActive{};
std::atomic_bool SortPanelSuppressedByOverlay{};
std::vector<std::string> OpenUiOverlayPanels{};
std::atomic_bool ReportedRowAccessFailure{};
std::atomic_bool DiagnosticCapturedThisScreen{};
std::atomic_bool DeferredUiUpdatePending{};
std::atomic_bool SortPanelSyncPending{};
std::atomic_bool SortModeApplyPending{};
std::atomic_bool DeferredUpdatesStartedThisScreen{};
std::atomic_bool DeferredDiagnosticCapturedThisScreen{};
std::atomic<std::uint32_t> DeferredUiUpdateBudget{};

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = "offline-character-order",
    .name = "Offline Character Order",
    .version = "0.9.29",
    .author = "MadMike",
    .description = "Sorts Offline characters with an in-game sort mode panel.",
    .flags = D2RL::PluginFlags::Client | D2RL::PluginFlags::NativeHooks,
};

auto MemoryRange(const void* pointer, std::size_t size, bool writable = false) noexcept -> bool {
    if (pointer == nullptr || size == 0) return false;
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    if (address > std::numeric_limits<std::uintptr_t>::max() - size) return false;
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(pointer, &info, sizeof(info)) != sizeof(info)
            || info.State != MEM_COMMIT
            || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
        return false;
    }
    const auto protection = info.Protect & 0xFF;
    const bool readable = protection == PAGE_READONLY
        || protection == PAGE_READWRITE
        || protection == PAGE_WRITECOPY
        || protection == PAGE_EXECUTE_READ
        || protection == PAGE_EXECUTE_READWRITE
        || protection == PAGE_EXECUTE_WRITECOPY;
    const bool canWrite = protection == PAGE_READWRITE
        || protection == PAGE_WRITECOPY
        || protection == PAGE_EXECUTE_READWRITE
        || protection == PAGE_EXECUTE_WRITECOPY;
    const auto regionStart = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
    const auto regionEnd = regionStart + info.RegionSize;
    if (address < regionStart || address + size > regionEnd) return false;
    return writable ? canWrite : readable;
}

template<class T>
auto ReadField(const void* object, std::uintptr_t offset, T& output) noexcept -> bool {
    if (object == nullptr) return false;
    const auto address = reinterpret_cast<std::uintptr_t>(object) + offset;
    if (address < reinterpret_cast<std::uintptr_t>(object)
            || !MemoryRange(reinterpret_cast<const void*>(address), sizeof(T))) {
        return false;
    }
    output = *reinterpret_cast<const T*>(address);
    return true;
}

template<class T>
auto WriteField(void* object, std::uintptr_t offset, T value) noexcept -> bool {
    if (object == nullptr) return false;
    const auto address = reinterpret_cast<std::uintptr_t>(object) + offset;
    if (address < reinterpret_cast<std::uintptr_t>(object)
            || !MemoryRange(reinterpret_cast<void*>(address), sizeof(T), true)) {
        return false;
    }
    *reinterpret_cast<T*>(address) = value;
    return true;
}

auto ReadAscii(const char* value, std::string& output, std::size_t maximum) -> bool {
    if (value == nullptr || maximum == 0) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(value);
    output.clear();
    output.reserve(std::min(maximum, MaximumNameLength));
    for (std::size_t index = 0; index < maximum; ++index) {
        if (base > std::numeric_limits<std::uintptr_t>::max() - index) return false;
        const auto* character = reinterpret_cast<const char*>(base + index);
        if (!MemoryRange(character, 1)) return false;
        const auto byte = static_cast<unsigned char>(*character);
        if (byte == 0) return !output.empty();
        if (byte < 0x20 || byte > 0x7E) return false;
        output.push_back(static_cast<char>(byte));
    }
    return false;
}

auto ReadWidgetLabel(void* widget, std::string& label) -> bool {
    std::uint64_t pointer{};
    return ReadField(widget, WidgetNameOffset, pointer)
        && ReadAscii(reinterpret_cast<const char*>(pointer), label, 64);
}

auto FindNamedWidget(void* root, const char* wanted) -> void* {
    if (root == nullptr || wanted == nullptr) return nullptr;
    std::array<WidgetNode, 256> pending{};
    std::array<void*, 256> visited{};
    std::size_t pendingCount = 0;
    std::size_t visitedCount = 0;
    std::uint64_t rootChildren{};
    std::int32_t rootCount{};
    if (!ReadField(root, WidgetChildrenOffset, rootChildren)
            || !ReadField(root, WidgetChildCountOffset, rootCount)
            || rootCount < 0 || rootCount > 128
            || (rootCount > 0 && !MemoryRange(
                reinterpret_cast<const void*>(rootChildren),
                static_cast<std::size_t>(rootCount) * sizeof(void*)))) {
        return nullptr;
    }
    for (std::int32_t index = 0; index < rootCount; ++index) {
        pending[pendingCount++] = {
            *reinterpret_cast<void**>(rootChildren + static_cast<std::uintptr_t>(index) * sizeof(void*)),
            1,
        };
    }
    while (pendingCount > 0 && visitedCount < visited.size()) {
        const auto entry = pending[--pendingCount];
        if (entry.widget == nullptr || entry.depth > 12
                || !MemoryRange(entry.widget, WidgetNameOffset + sizeof(void*))) {
            continue;
        }
        bool duplicate = false;
        for (std::size_t index = 0; index < visitedCount; ++index) {
            if (visited[index] == entry.widget) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;
        visited[visitedCount++] = entry.widget;

        std::string label;
        if (ReadWidgetLabel(entry.widget, label) && label == wanted) return entry.widget;

        std::uint64_t children{};
        std::int32_t childCount{};
        if (!ReadField(entry.widget, WidgetChildrenOffset, children)
                || !ReadField(entry.widget, WidgetChildCountOffset, childCount)
                || childCount <= 0 || childCount > 128
                || pendingCount + static_cast<std::size_t>(childCount) > pending.size()
                || !MemoryRange(reinterpret_cast<const void*>(children),
                    static_cast<std::size_t>(childCount) * sizeof(void*))) {
            continue;
        }
        for (std::int32_t index = 0; index < childCount; ++index) {
            pending[pendingCount++] = {
                *reinterpret_cast<void**>(children + static_cast<std::uintptr_t>(index) * sizeof(void*)),
                entry.depth + 1,
            };
        }
    }
    return nullptr;
}

auto ReadCharacterName(void* row, std::string& name) -> bool {
    auto* const widget = FindNamedWidget(row, "Name");
    if (widget == nullptr) return false;
    std::uint64_t vtable{};
    std::uint64_t textPointer{};
    return ReadField(widget, 0, vtable)
        && vtable == reinterpret_cast<std::uintptr_t>(Base) + TextWidgetVtableRva
        && ReadField(widget, TextWidgetValueOffset, textPointer)
        && ReadAscii(reinterpret_cast<const char*>(textPointer), name, MaximumNameLength);
}

auto EqualInsensitive(std::string_view left, std::string_view right) noexcept -> bool;

auto ReadTextBytes(const char* value, std::string& output,
        std::size_t maximum) -> bool {
    if (value == nullptr || maximum == 0) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(value);
    output.clear();
    output.reserve(maximum);
    for (std::size_t index = 0; index < maximum; ++index) {
        if (base > std::numeric_limits<std::uintptr_t>::max() - index) return false;
        const auto* const character = reinterpret_cast<const char*>(base + index);
        if (!MemoryRange(character, 1)) return false;
        const auto byte = static_cast<unsigned char>(*character);
        if (byte == 0) return !output.empty();
        if (byte < 0x20 || byte == 0x7F) return false;
        output.push_back(static_cast<char>(byte));
    }
    return false;
}

auto IsAsciiWordCharacter(unsigned char character) noexcept -> bool {
    return (character >= 'a' && character <= 'z')
        || (character >= 'A' && character <= 'Z')
        || (character >= '0' && character <= '9')
        || character == '_';
}

auto FindClassOrder(std::string_view text) noexcept -> std::int32_t {
    constexpr std::array<std::string_view, 8> ClassNames{
        "Amazon", "Assassin", "Barbarian", "Druid", "Necromancer",
        "Paladin", "Sorceress", "Warlock",
    };
    for (std::size_t classIndex = 0; classIndex < ClassNames.size(); ++classIndex) {
        const auto className = ClassNames[classIndex];
        if (text.size() < className.size()) continue;
        for (std::size_t start = 0; start + className.size() <= text.size(); ++start) {
            const auto end = start + className.size();
            if ((start > 0 && IsAsciiWordCharacter(
                    static_cast<unsigned char>(text[start - 1])))
                    || (end < text.size() && IsAsciiWordCharacter(
                        static_cast<unsigned char>(text[end])))) {
                continue;
            }
            if (EqualInsensitive(text.substr(start, className.size()), className)) {
                return static_cast<std::int32_t>(classIndex);
            }
        }
    }
    return -1;
}

void ReadCharacterMetadata(void* row, std::int32_t& level,
        std::int32_t& classOrder) {
    level = -1;
    classOrder = -1;
    auto* const widget = FindNamedWidget(row, "Level&Class");
    if (widget == nullptr) return;
    std::uint64_t vtable{};
    std::uint64_t textPointer{};
    std::string text;
    if (!ReadField(widget, 0, vtable)
            || vtable != reinterpret_cast<std::uintptr_t>(Base) + TextWidgetVtableRva
            || !ReadField(widget, TextWidgetValueOffset, textPointer)
            || !ReadTextBytes(reinterpret_cast<const char*>(textPointer), text,
                MaximumMetadataLength)) {
        return;
    }

    for (std::size_t index = 0; index < text.size();) {
        if (text[index] < '0' || text[index] > '9') {
            ++index;
            continue;
        }
        std::uint32_t value{};
        while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
            value = std::min(1000U, value * 10U
                + static_cast<std::uint32_t>(text[index] - '0'));
            ++index;
        }
        if (value >= 1U && value <= 99U) {
            level = static_cast<std::int32_t>(value);
            break;
        }
    }
    classOrder = FindClassOrder(text);
}

auto ActiveOfflinePanel(void* panel) noexcept -> bool {
    if (Base == nullptr || panel == nullptr) return false;
    std::uint64_t modeSelector{};
    std::uint64_t list{};
    std::int32_t selectedMode{};
    std::uint8_t listVisible{};
    if (!ReadField(panel, CharacterSelectModeSelectorOffset, modeSelector)
            || !ReadField(reinterpret_cast<void*>(modeSelector), ModeSelectedIndexOffset, selectedMode)
            || selectedMode != OfflineModeIndex
            || !ReadField(panel, CharacterSelectListOffset, list)
            || !ReadField(reinterpret_cast<void*>(list), 0x51, listVisible)
            || listVisible == 0) {
        return false;
    }
    return true;
}

auto EqualInsensitive(std::string_view left, std::string_view right) noexcept -> bool {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(left[index]))
                != std::tolower(static_cast<unsigned char>(right[index]))) {
            return false;
        }
    }
    return true;
}

auto CompareInsensitive(std::string_view left, std::string_view right) noexcept -> int {
    const auto common = std::min(left.size(), right.size());
    for (std::size_t index = 0; index < common; ++index) {
        const auto a = std::tolower(static_cast<unsigned char>(left[index]));
        const auto b = std::tolower(static_cast<unsigned char>(right[index]));
        if (a < b) return -1;
        if (a > b) return 1;
    }
    if (left.size() < right.size()) return -1;
    if (left.size() > right.size()) return 1;
    return 0;
}

auto FindConfigString(std::string_view config, std::string_view key,
        std::string& value) -> bool {
    const auto keyPosition = config.find(key);
    if (keyPosition == std::string_view::npos) return false;
    const auto lineEnd = config.find('\n', keyPosition);
    const auto endOfLine = lineEnd == std::string_view::npos ? config.size() : lineEnd;
    const auto equals = config.find('=', keyPosition + key.size());
    if (equals == std::string_view::npos || equals >= endOfLine) return false;
    const auto quote = config.find('"', equals + 1);
    if (quote == std::string_view::npos || quote >= endOfLine) return false;
    const auto end = config.find('"', quote + 1);
    if (end == std::string_view::npos || end >= endOfLine) return false;
    value.assign(config.substr(quote + 1, end - quote - 1));
    return true;
}

auto ParseCustomOrder(std::string_view config) -> std::vector<std::string> {
    std::vector<std::string> result;
    const auto keyPosition = config.find("custom_order");
    if (keyPosition == std::string_view::npos) return result;
    const auto open = config.find('[', keyPosition);
    const auto close = config.find(']', open == std::string_view::npos ? keyPosition : open + 1);
    if (open == std::string_view::npos || close == std::string_view::npos) return result;
    auto cursor = open + 1;
    while (cursor < close && result.size() < MaximumCharacterRows) {
        const auto quote = config.find('"', cursor);
        if (quote == std::string_view::npos || quote >= close) break;
        std::string value;
        cursor = quote + 1;
        while (cursor < close && config[cursor] != '"') {
            if (config[cursor] == '\\' && cursor + 1 < close) ++cursor;
            value.push_back(config[cursor++]);
        }
        if (cursor < close && config[cursor] == '"' && !value.empty()) {
            result.push_back(std::move(value));
            ++cursor;
        }
    }
    return result;
}

auto ParseMode(std::string_view text) noexcept -> SortMode {
    if (text == "name_descending") return SortMode::NameDescending;
    if (text == "most_recent" || text == "most_recently_played_descending") {
        return SortMode::MostRecent;
    }
    if (text == "level_ascending") return SortMode::LevelAscending;
    if (text == "level_descending") return SortMode::LevelDescending;
    if (text == "class" || text == "class_level_descending") {
        return SortMode::ClassLevelDescending;
    }
    if (text == "native") return SortMode::Native;
    if (text == "custom") return SortMode::Custom;
    return SortMode::NameAscending;
}

auto ModeName(SortMode mode) noexcept -> const char* {
    switch (mode) {
    case SortMode::MostRecent: return "most_recent";
    case SortMode::NameAscending: return "name_ascending";
    case SortMode::NameDescending: return "name_descending";
    case SortMode::LevelAscending: return "level_ascending";
    case SortMode::LevelDescending: return "level_descending";
    case SortMode::ClassLevelDescending: return "class_level_descending";
    case SortMode::Custom: return "custom";
    case SortMode::Native: return "native";
    }
    return "name_ascending";
}

auto ReadConfiguration() noexcept -> bool {
    if (Context == nullptr || !Context->EnsureConfig(DefaultConfig)) return false;
    std::array<char, MaximumConfigBytes> buffer{};
    std::uint32_t requiredSize{};
    if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &requiredSize)
            || requiredSize > buffer.size()) {
        Context->LogError("OfflineCharacterOrder: configuration could not be read or exceeds 64 KiB.");
        return false;
    }
    try {
        const std::string_view config(buffer.data());
        std::string mode;
        CurrentMode = FindConfigString(config, "sort_mode", mode)
            ? ParseMode(mode) : SortMode::NameAscending;
        CustomModeActive.store(CurrentMode == SortMode::Custom,
            std::memory_order_release);
        CustomOrderNames = ParseCustomOrder(config);
        return true;
    } catch (...) {
        Context->LogError("OfflineCharacterOrder: invalid configuration; no hook was installed.");
        return false;
    }
}

auto FindNamedChildArray(void* root, const char* wanted,
        void*& resultArray, std::int32_t& resultCount) -> bool {
    auto* const target = FindNamedWidget(root, wanted);
    std::uint64_t children{};
    std::int32_t childCount{};
    if (target == nullptr
            || !ReadField(target, WidgetChildrenOffset, children)
            || !ReadField(target, WidgetChildCountOffset, childCount)
            || childCount < 0 || childCount > MaximumCharacterRows
            || (childCount > 0 && !MemoryRange(reinterpret_cast<const void*>(children),
                static_cast<std::size_t>(childCount) * sizeof(void*)))) {
        return false;
    }
    resultArray = reinterpret_cast<void*>(children);
    resultCount = childCount;
    return true;
}

auto ReadRows(void* list, std::vector<CharacterRow>& rows,
        void*& arrayAddress, std::int32_t& selectedIndex) -> bool {
    std::uint64_t vector{};
    std::uint64_t array{};
    std::int32_t count{};
    if (list == nullptr
            || !ReadField(list, ListVectorOffset, vector)
            || !ReadField(reinterpret_cast<void*>(vector), VectorDataOffset, array)
            || !ReadField(reinterpret_cast<void*>(vector), VectorSizeOffset, count)
            || !ReadField(list, ListSelectedIndexOffset, selectedIndex)
            || count <= 0 || count > MaximumCharacterRows
            || !MemoryRange(reinterpret_cast<const void*>(array),
                static_cast<std::size_t>(count) * sizeof(void*), true)
            || !MemoryRange(reinterpret_cast<const std::uint8_t*>(list) + ListSelectedIndexOffset,
                sizeof(selectedIndex), true)) {
        return false;
    }

    void* containerArray{};
    std::int32_t containerCount{};
    if (!FindNamedChildArray(list, "Container", containerArray, containerCount)
            || containerArray != reinterpret_cast<void*>(array)
            || containerCount != count) {
        return false;
    }

    rows.clear();
    rows.reserve(static_cast<std::size_t>(count));
    std::uint32_t commonX{};
    std::uint32_t commonWidth{};
    std::uint32_t commonHeight{};
    std::array<std::uint32_t, MaximumCharacterRows> seenY{};
    for (std::int32_t index = 0; index < count; ++index) {
        auto* const row = *reinterpret_cast<void**>(array
            + static_cast<std::uintptr_t>(index) * sizeof(void*));
        std::uint64_t rowVtable{};
        std::string widgetLabel;
        std::string name;
        std::uint32_t x{};
        std::uint32_t y{};
        std::uint32_t width{};
        std::uint32_t height{};
        std::int32_t level{-1};
        std::int32_t classOrder{-1};
        if (row == nullptr
                || !MemoryRange(row, sizeof(void*))
                || !ReadField(row, 0, rowVtable)
                || rowVtable != reinterpret_cast<std::uintptr_t>(Base) + RowWidgetVtableRva
                || !ReadWidgetLabel(row, widgetLabel)
                || widgetLabel.rfind("ListItem", 0) != 0
                || !ReadCharacterName(row, name)
                || !ReadField(row, RowPositionOffset, x)
                || !ReadField(row, RowPositionOffset + sizeof(x), y)
                || !ReadField(row, RowSizeOffset, width)
                || !ReadField(row, RowSizeOffset + sizeof(width), height)
                || !MemoryRange(reinterpret_cast<std::uint8_t*>(row) + RowPositionOffset,
                    sizeof(x) + sizeof(y), true)
                || x > 8192 || y > 8192 || width == 0 || width > 8192
                || height == 0 || height > 8192) {
            return false;
        }
        ReadCharacterMetadata(row, level, classOrder);
        if (index == 0) {
            commonX = x;
            commonWidth = width;
            commonHeight = height;
        } else {
            if (x != commonX || width != commonWidth || height != commonHeight) {
                return false;
            }
            for (std::int32_t previous = 0; previous < index; ++previous) {
                if (seenY[static_cast<std::size_t>(previous)] == y) return false;
            }
        }
        seenY[static_cast<std::size_t>(index)] = y;
        rows.push_back({row, std::move(name), level, classOrder,
            x, y, width, height});
    }
    if (selectedIndex < -1 || selectedIndex >= count) return false;
    arrayAddress = reinterpret_cast<void*>(array);
    return true;
}

void LogMatchingTextWidgets(void* root, const std::vector<CharacterRow>& rows,
        const char* stage) noexcept {
    try {
        std::array<WidgetNode, 1024> pending{};
        std::array<void*, 1024> visited{};
        std::size_t pendingCount = 0;
        std::size_t visitedCount = 0;
        std::uint64_t rootChildren{};
        std::int32_t rootCount{};
        if (!ReadField(root, WidgetChildrenOffset, rootChildren)
                || !ReadField(root, WidgetChildCountOffset, rootCount)
                || rootCount < 0 || rootCount > 256
                || (rootCount > 0 && !MemoryRange(
                    reinterpret_cast<const void*>(rootChildren),
                    static_cast<std::size_t>(rootCount) * sizeof(void*)))) {
            D2RL::LogWarn(Context,
                "OfflineCharacterOrder 0.9.29: diagnostic root could not be enumerated.");
            return;
        }
        for (std::int32_t index = 0; index < rootCount; ++index) {
            pending[pendingCount++] = {
                *reinterpret_cast<void**>(rootChildren
                    + static_cast<std::uintptr_t>(index) * sizeof(void*)),
                1,
            };
        }
        while (pendingCount > 0 && visitedCount < visited.size()) {
            const auto entry = pending[--pendingCount];
            if (entry.widget == nullptr || entry.depth > 16
                    || !MemoryRange(entry.widget, WidgetNameOffset + sizeof(void*))) {
                continue;
            }
            bool duplicate = false;
            for (std::size_t index = 0; index < visitedCount; ++index) {
                if (visited[index] == entry.widget) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) continue;
            visited[visitedCount++] = entry.widget;

            std::uint64_t vtable{};
            std::uint64_t textPointer{};
            std::string text;
            if (ReadField(entry.widget, 0, vtable)
                    && vtable == reinterpret_cast<std::uintptr_t>(Base) + TextWidgetVtableRva
                    && ReadField(entry.widget, TextWidgetValueOffset, textPointer)
                    && ReadTextBytes(reinterpret_cast<const char*>(textPointer), text,
                        MaximumMetadataLength)) {
                const bool matchesRow = std::any_of(rows.begin(), rows.end(),
                    [&text](const auto& row) {
                        return EqualInsensitive(text, row.name);
                    });
                std::string widgetName;
                (void)ReadWidgetLabel(entry.widget, widgetName);
                if (matchesRow || (widgetName == "Level&Class"
                        && std::strcmp(stage, "before-sort") == 0)) {
                    std::uint64_t parent{};
                    std::uint32_t x{};
                    std::uint32_t y{};
                    std::uint8_t visibleFlag{};
                    (void)ReadField(entry.widget, WidgetParentOffset, parent);
                    (void)ReadField(entry.widget, RowPositionOffset, x);
                    (void)ReadField(entry.widget, RowPositionOffset + sizeof(x), y);
                    (void)ReadField(entry.widget, WidgetVisibleFlagOffset, visibleFlag);
                    D2RL::LogInfoF(Context,
                        "OfflineCharacterOrder 0.9.29: %s text-widget=%p name=%s text=%s parent=%p local=%u,%u visibleFlag=%u",
                        stage, entry.widget, widgetName.c_str(), text.c_str(),
                        reinterpret_cast<void*>(parent), x, y,
                        static_cast<unsigned>(visibleFlag));
                }
            }

            std::uint64_t children{};
            std::int32_t childCount{};
            if (!ReadField(entry.widget, WidgetChildrenOffset, children)
                    || !ReadField(entry.widget, WidgetChildCountOffset, childCount)
                    || childCount <= 0 || childCount > 256
                    || pendingCount + static_cast<std::size_t>(childCount) > pending.size()
                    || !MemoryRange(reinterpret_cast<const void*>(children),
                        static_cast<std::size_t>(childCount) * sizeof(void*))) {
                continue;
            }
            for (std::int32_t index = 0; index < childCount; ++index) {
                pending[pendingCount++] = {
                    *reinterpret_cast<void**>(children
                        + static_cast<std::uintptr_t>(index) * sizeof(void*)),
                    entry.depth + 1,
                };
            }
        }
        D2RL::LogInfoF(Context,
            "OfflineCharacterOrder 0.9.29: %s scanned=%zu pending=%zu",
            stage, visitedCount, pendingCount);
    } catch (...) {
        D2RL::LogWarn(Context,
            "OfflineCharacterOrder 0.9.29: text-widget diagnostics stopped safely.");
    }
}

void LogSelectionOverlaySnapshot(void* list, const char* stage,
        std::int32_t selectedIndex) noexcept {
    try {
        auto* const widget = FindNamedWidget(list, "Selection");
        if (widget == nullptr) {
            D2RL::LogInfoF(Context,
                "OfflineCharacterOrder 0.9.29: %s selection-widget=not-found selectedIndex=%d",
                stage, selectedIndex);
            return;
        }
        std::uint64_t vtable{};
        std::uint64_t children{};
        std::int32_t childCount{};
        std::uint32_t x{};
        std::uint32_t y{};
        std::uint32_t width{};
        std::uint32_t height{};
        std::uint8_t visibleFlag{};
        (void)ReadField(widget, 0, vtable);
        (void)ReadField(widget, WidgetChildrenOffset, children);
        (void)ReadField(widget, WidgetChildCountOffset, childCount);
        (void)ReadField(widget, RowPositionOffset, x);
        (void)ReadField(widget, RowPositionOffset + sizeof(x), y);
        (void)ReadField(widget, RowSizeOffset, width);
        (void)ReadField(widget, RowSizeOffset + sizeof(width), height);
        (void)ReadField(widget, WidgetVisibleFlagOffset, visibleFlag);
        const auto vtableRva = vtable >= reinterpret_cast<std::uintptr_t>(Base)
            ? vtable - reinterpret_cast<std::uintptr_t>(Base) : 0;
        D2RL::LogInfoF(Context,
            "OfflineCharacterOrder 0.9.29: %s selection-widget=%p vtableRva=0x%llX rect=%u,%u,%u,%u visibleFlag=%u selectedIndex=%d children=%d",
            stage, widget, static_cast<unsigned long long>(vtableRva),
            x, y, width, height, static_cast<unsigned>(visibleFlag),
            selectedIndex, childCount);
        if (childCount <= 0 || childCount > 32
                || !MemoryRange(reinterpret_cast<const void*>(children),
                    static_cast<std::size_t>(childCount) * sizeof(void*))) {
            return;
        }
        for (std::int32_t index = 0; index < childCount; ++index) {
            auto* const child = *reinterpret_cast<void**>(children
                + static_cast<std::uintptr_t>(index) * sizeof(void*));
            if (child == nullptr) continue;
            std::string childName;
            std::uint64_t childVtable{};
            std::uint32_t childX{};
            std::uint32_t childY{};
            std::uint32_t childWidth{};
            std::uint32_t childHeight{};
            std::uint8_t childVisible{};
            (void)ReadWidgetLabel(child, childName);
            (void)ReadField(child, 0, childVtable);
            (void)ReadField(child, RowPositionOffset, childX);
            (void)ReadField(child, RowPositionOffset + sizeof(childX), childY);
            (void)ReadField(child, RowSizeOffset, childWidth);
            (void)ReadField(child, RowSizeOffset + sizeof(childWidth), childHeight);
            (void)ReadField(child, WidgetVisibleFlagOffset, childVisible);
            const auto childVtableRva = childVtable >= reinterpret_cast<std::uintptr_t>(Base)
                ? childVtable - reinterpret_cast<std::uintptr_t>(Base) : 0;
            D2RL::LogInfoF(Context,
                "OfflineCharacterOrder 0.9.29: %s selection-child[%d]=%p name=%s vtableRva=0x%llX rect=%u,%u,%u,%u visibleFlag=%u",
                stage, index, child, childName.c_str(),
                static_cast<unsigned long long>(childVtableRva),
                childX, childY, childWidth, childHeight,
                static_cast<unsigned>(childVisible));
        }
    } catch (...) {
        D2RL::LogWarn(Context,
            "OfflineCharacterOrder 0.9.29: selection-widget diagnostics stopped safely.");
    }
}

void LogRowVisualChildren(void* list, const char* stage) noexcept {
    try {
        std::vector<CharacterRow> rows;
        void* array{};
        std::int32_t selectedIndex{};
        if (!ReadRows(list, rows, array, selectedIndex)) {
            D2RL::LogWarn(Context,
                "OfflineCharacterOrder 0.9.29: row-child diagnostic could not validate rows.");
            return;
        }
        for (std::size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
            std::uint64_t rowVtable{};
            std::uint64_t children{};
            std::int32_t childCount{};
            (void)ReadField(rows[rowIndex].widget, 0, rowVtable);
            if (!ReadField(rows[rowIndex].widget, WidgetChildrenOffset, children)
                    || !ReadField(rows[rowIndex].widget, WidgetChildCountOffset, childCount)
                    || childCount < 0 || childCount > 64
                    || (childCount > 0 && !MemoryRange(
                        reinterpret_cast<const void*>(children),
                        static_cast<std::size_t>(childCount) * sizeof(void*)))) {
                continue;
            }
            const auto rowVtableRva = rowVtable >= reinterpret_cast<std::uintptr_t>(Base)
                ? rowVtable - reinterpret_cast<std::uintptr_t>(Base) : 0;
            D2RL::LogInfoF(Context,
                "OfflineCharacterOrder 0.9.29: %s row[%zu] name=%s widget=%p vtableRva=0x%llX children=%d rect=%u,%u,%u,%u",
                stage, rowIndex, rows[rowIndex].name.c_str(), rows[rowIndex].widget,
                static_cast<unsigned long long>(rowVtableRva), childCount,
                rows[rowIndex].x, rows[rowIndex].y,
                rows[rowIndex].width, rows[rowIndex].height);
            for (std::int32_t childIndex = 0; childIndex < childCount; ++childIndex) {
                auto* const child = *reinterpret_cast<void**>(children
                    + static_cast<std::uintptr_t>(childIndex) * sizeof(void*));
                if (child == nullptr) continue;
                std::string childName;
                std::uint64_t childVtable{};
                std::uint32_t x{};
                std::uint32_t y{};
                std::uint32_t width{};
                std::uint32_t height{};
                std::uint8_t visibleFlag{};
                std::int32_t grandchildCount{};
                (void)ReadWidgetLabel(child, childName);
                (void)ReadField(child, 0, childVtable);
                (void)ReadField(child, RowPositionOffset, x);
                (void)ReadField(child, RowPositionOffset + sizeof(x), y);
                (void)ReadField(child, RowSizeOffset, width);
                (void)ReadField(child, RowSizeOffset + sizeof(width), height);
                (void)ReadField(child, WidgetVisibleFlagOffset, visibleFlag);
                (void)ReadField(child, WidgetChildCountOffset, grandchildCount);
                const auto childVtableRva = childVtable >= reinterpret_cast<std::uintptr_t>(Base)
                    ? childVtable - reinterpret_cast<std::uintptr_t>(Base) : 0;
                D2RL::LogInfoF(Context,
                    "OfflineCharacterOrder 0.9.29: %s row[%zu].child[%d]=%p name=%s vtableRva=0x%llX rect=%u,%u,%u,%u visibleFlag=%u children=%d",
                    stage, rowIndex, childIndex, child, childName.c_str(),
                    static_cast<unsigned long long>(childVtableRva),
                    x, y, width, height, static_cast<unsigned>(visibleFlag),
                    grandchildCount);
            }
        }
    } catch (...) {
        D2RL::LogWarn(Context,
            "OfflineCharacterOrder 0.9.29: row-child diagnostics stopped safely.");
    }
}

auto FormatRowSnapshot(void* list, char* output, std::size_t outputSize,
        std::int32_t& selectedIndex) -> bool {
    if (output == nullptr || outputSize == 0) return false;
    output[0] = '\0';
    std::uint64_t vector{};
    std::uint64_t array{};
    std::int32_t count{};
    if (!ReadField(list, ListVectorOffset, vector)
            || !ReadField(reinterpret_cast<void*>(vector), VectorDataOffset, array)
            || !ReadField(reinterpret_cast<void*>(vector), VectorSizeOffset, count)
            || !ReadField(list, ListSelectedIndexOffset, selectedIndex)
            || count <= 0 || count > MaximumCharacterRows
            || !MemoryRange(reinterpret_cast<const void*>(array),
                static_cast<std::size_t>(count) * sizeof(void*))) {
        return false;
    }
    std::size_t used = 0;
    for (std::int32_t index = 0; index < count; ++index) {
        auto* const row = *reinterpret_cast<void**>(array
            + static_cast<std::uintptr_t>(index) * sizeof(void*));
        std::string name;
        std::uint32_t y{};
        std::uint64_t rowVtable{};
        if (row == nullptr
                || !ReadField(row, 0, rowVtable)
                || rowVtable != reinterpret_cast<std::uintptr_t>(Base) + RowWidgetVtableRva
                || !ReadCharacterName(row, name)
                || !ReadField(row, RowPositionOffset + sizeof(std::uint32_t), y)) {
            return false;
        }
        const auto written = std::snprintf(output + used, outputSize - used,
            "%s%s=%p@%u", index == 0 ? "" : ",", name.c_str(), row, y);
        if (written < 0 || static_cast<std::size_t>(written) >= outputSize - used) {
            return false;
        }
        used += static_cast<std::size_t>(written);
    }
    return true;
}

void LogRowSnapshot(const char* stage, void* list) noexcept {
    try {
        std::array<char, 2048> rows{};
        std::int32_t selectedIndex = -2;
        const bool readable = FormatRowSnapshot(
            list, rows.data(), rows.size(), selectedIndex);
        D2RL::LogInfoF(Context,
            "OfflineCharacterOrder 0.9.29: %s selectedIndex=%d rows=%s",
            stage, selectedIndex, readable ? rows.data() : "unreadable");
        if (readable) {
            LogSelectionOverlaySnapshot(list, stage, selectedIndex);
            if (std::strcmp(stage, "before-sort") == 0
                    || std::strcmp(stage, "deferred-after-update") == 0) {
                LogRowVisualChildren(list, stage);
            }
        }
    } catch (...) {
        D2RL::LogWarn(Context, "OfflineCharacterOrder 0.9.29: row snapshot could not be formatted.");
    }
}

auto Names(const std::vector<CharacterRow>& rows) -> std::vector<std::string> {
    std::vector<std::string> result;
    result.reserve(rows.size());
    for (const auto& row : rows) result.push_back(row.name);
    return result;
}

auto ReconcileKnownOrder(const std::vector<std::string>& previousOrder,
        const std::vector<std::string>& observedNames) -> std::vector<std::string> {
    std::vector<std::string> result;
    result.reserve(observedNames.size());
    const auto appendIfPresent = [&result, &observedNames](const std::string& name) {
        const bool isPresent = std::any_of(observedNames.begin(), observedNames.end(),
            [&name](const auto& observed) { return EqualInsensitive(name, observed); });
        const bool alreadyAdded = std::any_of(result.begin(), result.end(),
            [&name](const auto& existing) { return EqualInsensitive(name, existing); });
        if (isPresent && !alreadyAdded) result.push_back(name);
    };
    for (const auto& name : previousOrder) appendIfPresent(name);
    for (const auto& name : observedNames) appendIfPresent(name);
    return result;
}

auto WidgetSignature(const std::vector<CharacterRow>& rows)
        -> std::vector<DatasetRowSignature> {
    std::vector<DatasetRowSignature> signature;
    signature.reserve(rows.size());
    for (const auto& row : rows) {
        signature.push_back({reinterpret_cast<std::uintptr_t>(row.widget),
            row.name, row.level, row.classOrder});
    }
    std::sort(signature.begin(), signature.end(), [](const auto& left, const auto& right) {
        return left.widget < right.widget;
    });
    return signature;
}

auto RankIn(const std::vector<std::string>& order, std::string_view name) noexcept -> std::size_t {
    for (std::size_t index = 0; index < order.size(); ++index) {
        if (EqualInsensitive(order[index], name)) return index;
    }
    return std::numeric_limits<std::size_t>::max();
}

void CaptureNativeLayout(void* list, const std::vector<CharacterRow>& rows,
        std::vector<DatasetRowSignature> signature, std::int32_t selectedIndex) {
    // Character deletion can rebuild row contents while retaining widget objects.
    // Keep the known native index order for surviving characters; the row
    // widgets may already occupy positions from the previous visual sort.
    NativeOrderNames = ReconcileKnownOrder(NativeOrderNames, Names(rows));
    SlotPositions.clear();
    SlotPositions.reserve(rows.size());
    for (const auto& row : rows) SlotPositions.push_back({row.x, row.y});
    std::stable_sort(SlotPositions.begin(), SlotPositions.end(),
        [](const auto& left, const auto& right) {
            if (left.y != right.y) return left.y < right.y;
            return left.x < right.x;
        });
    SelectionOffsetKnown = false;
    SelectionOffsetY = 0;
    auto* const selection = FindNamedWidget(list, "Selection");
    auto* const container = FindNamedWidget(list, "Container");
    std::int32_t selectionY{};
    std::int32_t containerY{};
    if (selection != nullptr && container != nullptr && !rows.empty()
            && ReadField(selection, RowPositionOffset + sizeof(std::int32_t), selectionY)
            && ReadField(container, RowPositionOffset + sizeof(std::int32_t), containerY)) {
        std::uint64_t nearestDistance = std::numeric_limits<std::uint64_t>::max();
        std::int64_t nearestOffset{};
        std::size_t nearestSlot{};
        for (std::size_t index = 0; index < rows.size(); ++index) {
            const auto rowY = static_cast<std::int64_t>(containerY)
                + static_cast<std::int64_t>(rows[index].y);
            const auto offset = static_cast<std::int64_t>(selectionY) - rowY;
            const auto distance = static_cast<std::uint64_t>(offset >= 0 ? offset : -offset);
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearestOffset = offset;
                nearestSlot = index;
            }
        }
        const auto maxRowRelativeOffset = static_cast<std::uint64_t>(
            rows[nearestSlot].height == 0 ? 1 : rows[nearestSlot].height);
        if (nearestDistance <= maxRowRelativeOffset) {
            SelectionOffsetY = static_cast<std::int32_t>(nearestOffset);
            SelectionOffsetKnown = true;
            if (selectedIndex < 0 || nearestSlot != static_cast<std::size_t>(selectedIndex)) {
                D2RL::LogInfoF(Context,
                    "OfflineCharacterOrder 0.9.29: selector geometry matched native slot=%zu while selectedIndex=%d; using row-relative offset=%d",
                    nearestSlot, selectedIndex, SelectionOffsetY);
            }
        }
    }
    DatasetSignature = std::move(signature);
    CachedListWidget = list;
    DatasetKnown = true;
}

auto SyncSelectionOverlay(void* list, std::int32_t selectedIndex) noexcept -> bool {
    if (!SelectionOffsetKnown || selectedIndex < 0
            || static_cast<std::size_t>(selectedIndex) >= SlotPositions.size()) {
        return false;
    }
    auto* const selection = FindNamedWidget(list, "Selection");
    auto* const container = FindNamedWidget(list, "Container");
    std::int32_t containerY{};
    std::int32_t currentY{};
    if (selection == nullptr || container == nullptr
            || !ReadField(container, RowPositionOffset + sizeof(std::int32_t), containerY)
            || !ReadField(selection, RowPositionOffset + sizeof(std::int32_t), currentY)
            || !MemoryRange(reinterpret_cast<std::uint8_t*>(selection)
                + RowPositionOffset + sizeof(std::int32_t), sizeof(currentY), true)) {
        return false;
    }
    const auto targetY = static_cast<std::int64_t>(containerY)
        + static_cast<std::int64_t>(SlotPositions[static_cast<std::size_t>(selectedIndex)].y)
        + SelectionOffsetY;
    if (targetY < -8192 || targetY > 8192) return false;
    if (currentY == targetY) return true;
    return WriteField(selection, RowPositionOffset + sizeof(std::int32_t),
        static_cast<std::int32_t>(targetY));
}

void ApplyMode(std::vector<CharacterRow>& rows) {
    if (CurrentMode == SortMode::NameAscending) {
        std::stable_sort(rows.begin(), rows.end(), [](const auto& left, const auto& right) {
            return CompareInsensitive(left.name, right.name) < 0;
        });
        return;
    }
    if (CurrentMode == SortMode::NameDescending) {
        std::stable_sort(rows.begin(), rows.end(), [](const auto& left, const auto& right) {
            return CompareInsensitive(left.name, right.name) > 0;
        });
        return;
    }
    if (CurrentMode == SortMode::LevelAscending
            || CurrentMode == SortMode::LevelDescending) {
        const bool descending = CurrentMode == SortMode::LevelDescending;
        std::stable_sort(rows.begin(), rows.end(), [descending](const auto& left, const auto& right) {
            const bool leftHasLevel = left.level >= 1 && left.level <= 99;
            const bool rightHasLevel = right.level >= 1 && right.level <= 99;
            if (leftHasLevel != rightHasLevel) return leftHasLevel;
            if (!leftHasLevel || left.level == right.level) return false;
            return descending ? left.level > right.level : left.level < right.level;
        });
        return;
    }
    if (CurrentMode == SortMode::ClassLevelDescending) {
        std::stable_sort(rows.begin(), rows.end(), [](const auto& left, const auto& right) {
            const auto leftClass = left.classOrder >= 0 && left.classOrder < 8
                ? left.classOrder : 8;
            const auto rightClass = right.classOrder >= 0 && right.classOrder < 8
                ? right.classOrder : 8;
            if (leftClass != rightClass) return leftClass < rightClass;
            const bool leftHasLevel = left.level >= 1 && left.level <= 99;
            const bool rightHasLevel = right.level >= 1 && right.level <= 99;
            if (leftHasLevel != rightHasLevel) return leftHasLevel;
            return leftHasLevel && left.level > right.level;
        });
        return;
    }
    const auto& order = CurrentMode == SortMode::Custom && !CustomOrderNames.empty()
        ? CustomOrderNames : NativeOrderNames;
    std::stable_sort(rows.begin(), rows.end(), [&order](const auto& left, const auto& right) {
        return RankIn(order, left.name) < RankIn(order, right.name);
    });
}

auto FindNativeSelectedRow(std::vector<CharacterRow>& rows,
        std::int32_t selectedIndex) -> std::vector<CharacterRow>::iterator {
    if (selectedIndex < 0) return rows.end();
    if (static_cast<std::size_t>(selectedIndex) < NativeOrderNames.size()) {
        const auto& selectedName = NativeOrderNames[
            static_cast<std::size_t>(selectedIndex)];
        const auto selected = std::find_if(rows.begin(), rows.end(),
            [&selectedName](const auto& row) {
                return EqualInsensitive(row.name, selectedName);
            });
        if (selected != rows.end()) return selected;
    }
    if (static_cast<std::size_t>(selectedIndex) < rows.size()) {
        return rows.begin() + selectedIndex;
    }
    return rows.end();
}

auto UpdateRows(void* list, bool* existingDatasetChanged = nullptr) -> bool {
    if (existingDatasetChanged != nullptr) *existingDatasetChanged = false;
    std::vector<CharacterRow> rows;
    void* arrayAddress{};
    std::int32_t selectedIndex{};
    if (!ReadRows(list, rows, arrayAddress, selectedIndex)) return false;
    (void)arrayAddress;

    auto signature = WidgetSignature(rows);
    const bool datasetChanged = !DatasetKnown || CachedListWidget != list
        || signature != DatasetSignature;
    if (existingDatasetChanged != nullptr && DatasetKnown && datasetChanged) {
        *existingDatasetChanged = true;
    }
    if (SelectedCharacterName.empty() && SelectedCharacterWidget != nullptr) {
        const auto previousSelection = std::find_if(rows.begin(), rows.end(),
            [](const auto& row) { return row.widget == SelectedCharacterWidget; });
        if (previousSelection != rows.end()) {
            SelectedCharacterName = previousSelection->name;
        }
    }
    if (datasetChanged) {
        CaptureNativeLayout(list, rows, std::move(signature), selectedIndex);
        LastObservedNativeSelectedIndex = selectedIndex;
    }
    if (SlotPositions.size() != rows.size()
            || NativeOrderNames.size() != rows.size()) return false;

    const auto selectedWidgetPresent = [&rows](void* widget) {
        return widget != nullptr && std::any_of(rows.begin(), rows.end(),
            [widget](const auto& row) { return row.widget == widget; });
    };
    if (datasetChanged || !selectedWidgetPresent(SelectedCharacterWidget)) {
        auto selectedRow = rows.end();
        if (!SelectedCharacterName.empty()) {
            selectedRow = std::find_if(rows.begin(), rows.end(),
                [](const auto& row) {
                    return EqualInsensitive(row.name, SelectedCharacterName);
                });
        }
        if (selectedRow == rows.end()) {
            selectedRow = FindNativeSelectedRow(rows, selectedIndex);
        }
        SelectedCharacterWidget = selectedRow != rows.end()
            ? selectedRow->widget : nullptr;
        SelectedCharacterName = selectedRow != rows.end()
            ? selectedRow->name : std::string{};
    } else if (SelectedCharacterName.empty()) {
        const auto selectedRow = std::find_if(rows.begin(), rows.end(),
            [](const auto& row) { return row.widget == SelectedCharacterWidget; });
        if (selectedRow != rows.end()) SelectedCharacterName = selectedRow->name;
    }
    void* const selectedWidget = SelectedCharacterWidget;
    auto displayedRows = rows;
    ApplyMode(displayedRows);
    std::int32_t displaySelectedIndex = -1;
    if (selectedWidget != nullptr) {
        for (std::size_t index = 0; index < displayedRows.size(); ++index) {
            if (displayedRows[index].widget == selectedWidget) {
                displaySelectedIndex = static_cast<std::int32_t>(index);
                break;
            }
        }
        if (displaySelectedIndex < 0) return false;
    } else if (selectedIndex >= 0) {
        const auto selected = FindNativeSelectedRow(rows, selectedIndex);
        if (selected != rows.end()) {
            for (std::size_t index = 0; index < displayedRows.size(); ++index) {
                if (displayedRows[index].widget == selected->widget) {
                    displaySelectedIndex = static_cast<std::int32_t>(index);
                    break;
                }
            }
        }
    }

    bool orderChanged = false;
    bool positionsChanged = false;
    for (std::size_t index = 0; index < displayedRows.size(); ++index) {
        if (rows[index].widget != displayedRows[index].widget) orderChanged = true;
        if (displayedRows[index].x != SlotPositions[index].x
                || displayedRows[index].y != SlotPositions[index].y) {
            positionsChanged = true;
        }
    }

    if (positionsChanged) {
        for (std::size_t index = 0; index < displayedRows.size(); ++index) {
            if (!WriteField(displayedRows[index].widget, RowPositionOffset, SlotPositions[index].x)
                    || !WriteField(displayedRows[index].widget, RowPositionOffset + sizeof(std::uint32_t),
                        SlotPositions[index].y)) {
                return false;
            }
        }
    }
    if (displaySelectedIndex >= 0) {
        if (!SelectionOffsetKnown || !SyncSelectionOverlay(list, displaySelectedIndex)) {
            D2RL::LogWarn(Context,
            "OfflineCharacterOrder 0.9.29: selection overlay could not be synchronized; row order remains applied.");
        } else if (selectedWidget != nullptr
                && selectedWidget != LastSyncedSelectionWidget) {
            auto* const selection = FindNamedWidget(list, "Selection");
            std::int32_t overlayY{};
            if (selection != nullptr
                    && ReadField(selection, RowPositionOffset + sizeof(std::int32_t), overlayY)) {
                D2RL::LogInfoF(Context,
                    "OfflineCharacterOrder 0.9.29: selection synced character=%s index=%d rowY=%u overlayY=%d offset=%d",
                    displayedRows[static_cast<std::size_t>(displaySelectedIndex)].name.c_str(),
                    displaySelectedIndex,
                    SlotPositions[static_cast<std::size_t>(displaySelectedIndex)].y,
                    overlayY, SelectionOffsetY);
            }
            LastSyncedSelectionWidget = selectedWidget;
        }
    }

    if (positionsChanged) {
        D2RL::LogInfoF(Context,
            "OfflineCharacterOrder 0.9.29: applied mode=%s rows=%zu visualOrderChanged=%d positionsChanged=%d nativeSelectedIndex=%d displaySelectedIndex=%d",
            ModeName(CurrentMode), rows.size(), orderChanged ? 1 : 0,
            positionsChanged ? 1 : 0, selectedIndex, displaySelectedIndex);
    }
    return true;
}

auto PersistCurrentSortMode() noexcept -> bool {
    if (Context == nullptr) return false;
    std::array<char, MaximumConfigBytes> buffer{};
    std::uint32_t requiredSize{};
    if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()),
            &requiredSize)
            || requiredSize > buffer.size()) {
        return false;
    }
    try {
        std::string config(buffer.data());
        const auto replacement = std::string("sort_mode = \"")
            + ModeName(CurrentMode) + "\"";
        bool replaced = false;
        std::size_t lineStart = 0;
        while (lineStart <= config.size()) {
            const auto lineEnd = config.find('\n', lineStart);
            const auto end = lineEnd == std::string::npos ? config.size() : lineEnd;
            auto first = lineStart;
            while (first < end && (config[first] == ' ' || config[first] == '\t')) ++first;
            constexpr std::string_view key = "sort_mode";
            const auto afterKey = first + key.size();
            if (afterKey <= end
                    && std::string_view(config).substr(first, key.size()) == key
                    && (afterKey == end || config[afterKey] == ' '
                        || config[afterKey] == '\t' || config[afterKey] == '=')) {
                const auto equals = config.find('=', afterKey);
                const auto quote = equals == std::string::npos || equals >= end
                    ? std::string::npos : config.find('"', equals + 1);
                const auto quoteEnd = quote == std::string::npos || quote >= end
                    ? std::string::npos : config.find('"', quote + 1);
                if (quote != std::string::npos && quoteEnd != std::string::npos
                        && quoteEnd < end) {
                    config.replace(first, quoteEnd + 1 - first, replacement);
                    replaced = true;
                }
                break;
            }
            if (lineEnd == std::string::npos) break;
            lineStart = lineEnd + 1;
        }
        if (!replaced) {
            if (!config.empty() && config.back() != '\n') config.push_back('\n');
            config += replacement;
            config.push_back('\n');
        }
        return Context->WriteConfig(config.c_str());
    } catch (...) {
        return false;
    }
}

auto PersistCustomOrder() noexcept -> bool {
    if (Context == nullptr) return false;
    std::array<char, MaximumConfigBytes> buffer{};
    std::uint32_t requiredSize{};
    if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()),
            &requiredSize)
            || requiredSize > buffer.size()) {
        return false;
    }
    try {
        std::string array = "[";
        for (std::size_t index = 0; index < CustomOrderNames.size(); ++index) {
            if (index > 0) array += ", ";
            array.push_back('"');
            for (const char character : CustomOrderNames[index]) {
                if (character == '\\' || character == '"') array.push_back('\\');
                array.push_back(character);
            }
            array.push_back('"');
        }
        array.push_back(']');

        std::string config(buffer.data());
        bool replaced = false;
        std::size_t lineStart = 0;
        while (lineStart <= config.size()) {
            const auto lineEnd = config.find('\n', lineStart);
            const auto end = lineEnd == std::string::npos ? config.size() : lineEnd;
            auto first = lineStart;
            while (first < end && (config[first] == ' ' || config[first] == '\t')) ++first;
            constexpr std::string_view key = "custom_order";
            const auto afterKey = first + key.size();
            if (afterKey <= end
                    && std::string_view(config).substr(first, key.size()) == key
                    && (afterKey == end || config[afterKey] == ' '
                        || config[afterKey] == '\t' || config[afterKey] == '=')) {
                const auto equals = config.find('=', afterKey);
                const auto open = equals == std::string::npos || equals >= end
                    ? std::string::npos : config.find('[', equals + 1);
                const auto close = open == std::string::npos || open >= end
                    ? std::string::npos : config.find(']', open + 1);
                if (open != std::string::npos && close != std::string::npos
                        && close < end) {
                    config.replace(open, close - open + 1, array);
                    replaced = true;
                }
                break;
            }
            if (lineEnd == std::string::npos) break;
            lineStart = lineEnd + 1;
        }
        if (!replaced) {
            if (!config.empty() && config.back() != '\n') config.push_back('\n');
            config += "custom_order = ";
            config += array;
            config.push_back('\n');
        }
        return Context->WriteConfig(config.c_str());
    } catch (...) {
        return false;
    }
}

auto LoadEmbeddedResource(std::uint16_t resourceId,
        std::vector<std::uint8_t>& bytes) noexcept -> bool {
    const auto module = reinterpret_cast<HMODULE>(&__ImageBase);
    const auto resource = FindResourceW(
        module, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (resource == nullptr) return false;
    const auto size = SizeofResource(module, resource);
    const auto loaded = LoadResource(module, resource);
    const auto* const data = loaded != nullptr
        ? static_cast<const std::uint8_t*>(LockResource(loaded)) : nullptr;
    if (data == nullptr || size == 0) return false;
    try {
        bytes.assign(data, data + size);
        return true;
    } catch (...) {
        return false;
    }
}

auto RegisterSortPanelResource(const D2RL::PluginContext* context,
        const char* path, const void* bytes, std::uint64_t byteCount,
        D2RL::Resources::RegistrationHandle& handle) noexcept -> bool {
    const D2RL::Resources::ResourceRegistration resource{
        .structSize = D2RL::Resources::ResourceRegistrationSize,
        .flags = 0,
        .path = path,
        .bytes = bytes,
        .byteCount = byteCount,
    };
    return SortPanelResourceService->registerResource(
            context, &resource, &handle) == D2RL::Resources::Result::Success
        && handle != D2RL::Resources::InvalidHandle;
}

void UnregisterSortPanelResources() noexcept {
    if (Context == nullptr || SortPanelResourceService == nullptr
            || SortPanelResourceService->unregisterResource == nullptr) {
        return;
    }
    for (auto handle = SortPanelArtworkHandles.rbegin();
            handle != SortPanelArtworkHandles.rend(); ++handle) {
        if (*handle != D2RL::Resources::InvalidHandle) {
            (void)SortPanelResourceService->unregisterResource(Context, *handle);
            *handle = D2RL::Resources::InvalidHandle;
        }
    }
    if (SortPanelLayoutResourceHandle != D2RL::Resources::InvalidHandle) {
        (void)SortPanelResourceService->unregisterResource(
            Context, SortPanelLayoutResourceHandle);
        SortPanelLayoutResourceHandle = D2RL::Resources::InvalidHandle;
    }
}

auto IsSortPanelOpen(bool& open) noexcept -> bool {
    open = false;
    if (Context == nullptr || SortPanelService == nullptr
            || SortPanelRegistrationHandle == D2RL::Panels::InvalidHandle
            || SortPanelService->getPanelInfo == nullptr) {
        return false;
    }
    D2RL::Panels::PanelInfo info{
        .structSize = D2RL::Panels::PanelInfoSize,
    };
    if (SortPanelService->getPanelInfo(Context, SortPanelRegistrationHandle, &info)
            != D2RL::Panels::Result::Success
            || info.presentationState == D2RL::Panels::PresentationState::Unknown) {
        return false;
    }
    open = info.presentationState == D2RL::Panels::PresentationState::Open;
    return true;
}

void RefreshSortPanelLabel() noexcept {
    if (Context == nullptr || SortPanelWidgetService == nullptr
            || SortPanelWidgetService->findPanel == nullptr
            || SortPanelWidgetService->findWidget == nullptr
            || SortPanelWidgetService->setWidgetVisible == nullptr) {
        return;
    }
    D2RL::Widgets::WidgetHandle panel{};
    if (SortPanelWidgetService->findPanel(Context,
            OfflineCharacterOrder::SortPanel::QualifiedName, &panel)
            != D2RL::Widgets::Result::Success) {
        return;
    }
    const auto selectedLabel = SortModeLabelIndex(CurrentMode);
    for (std::size_t index = 0; index < SortModeLabelWidgets.size(); ++index) {
        const auto name = SortModeLabelWidgets[index];
        const std::string widgetName{name};
        D2RL::Widgets::WidgetHandle label{};
        if (SortPanelWidgetService->findWidget(Context, panel,
                widgetName.c_str(), &label) == D2RL::Widgets::Result::Success) {
            (void)SortPanelWidgetService->setWidgetVisible(Context, label,
                index == selectedLabel);
        }
    }
}

void __cdecl CustomOrderMoveUiCallback(
        const D2RL::PluginContext* context, void* userData) noexcept {
    const auto* const action = static_cast<const CustomOrderInputAction*>(userData);
    if (context == nullptr || context != Context || action == nullptr
            || !Operational.load(std::memory_order_acquire)
            || !OfflineSelectionActive.load(std::memory_order_acquire)
            || !CustomModeActive.load(std::memory_order_acquire)
            || (action->direction != -1 && action->direction != 1)) {
        return;
    }
    auto* const panel = ActiveOfflinePanelWidget;
    if (!ActiveOfflinePanel(panel)) return;
    std::uint64_t list{};
    if (!ReadField(panel, CharacterSelectListOffset, list) || list == 0) return;

    try {
        auto* const characterList = reinterpret_cast<void*>(list);
        if (!UpdateRows(characterList)) {
            Context->LogWarn("OfflineCharacterOrder: custom move skipped because the current character list failed validation.");
            return;
        }
        std::vector<CharacterRow> rows;
        void* arrayAddress{};
        std::int32_t selectedIndex{};
        if (!ReadRows(characterList, rows, arrayAddress, selectedIndex)) {
            Context->LogWarn("OfflineCharacterOrder: custom move skipped because the current character rows could not be read.");
            return;
        }
        (void)arrayAddress;

        auto displayedRows = rows;
        ApplyMode(displayedRows);
        auto selected = std::find_if(displayedRows.begin(), displayedRows.end(), [](const auto& row) {
            return row.widget == SelectedCharacterWidget;
        });
        if (selected == displayedRows.end() && selectedIndex >= 0
                && static_cast<std::size_t>(selectedIndex) < rows.size()) {
            selected = FindNativeSelectedRow(rows, selectedIndex);
            if (selected != rows.end()) {
                const auto selectedWidget = selected->widget;
                selected = std::find_if(displayedRows.begin(), displayedRows.end(),
                    [selectedWidget](const auto& row) {
                        return row.widget == selectedWidget;
                    });
                if (selected != displayedRows.end()) {
                    SelectedCharacterWidget = selected->widget;
                    SelectedCharacterName = selected->name;
                }
            }
        }
        if (selected == displayedRows.end()) return;

        const auto selectedRow = static_cast<std::ptrdiff_t>(selected - displayedRows.begin());
        const auto targetRow = selectedRow + action->direction;
        if (targetRow < 0 || targetRow >= static_cast<std::ptrdiff_t>(displayedRows.size())) return;

        auto updatedOrder = Names(displayedRows);
        for (const auto& existingName : CustomOrderNames) {
            const bool alreadyPresent = std::any_of(updatedOrder.begin(), updatedOrder.end(),
                [&existingName](const auto& name) {
                    return EqualInsensitive(name, existingName);
                });
            if (!alreadyPresent && updatedOrder.size() < MaximumCharacterRows) {
                updatedOrder.push_back(existingName);
            }
        }
        std::swap(updatedOrder[static_cast<std::size_t>(selectedRow)],
            updatedOrder[static_cast<std::size_t>(targetRow)]);

        auto previousOrder = std::move(CustomOrderNames);
        CustomOrderNames = std::move(updatedOrder);
        if (!UpdateRows(characterList)) {
            CustomOrderNames = std::move(previousOrder);
            (void)UpdateRows(characterList);
            Context->LogWarn("OfflineCharacterOrder: custom move could not be applied; the saved order was left unchanged.");
            return;
        }
        if (!PersistCustomOrder()) {
            Context->LogWarn("OfflineCharacterOrder: custom order changed for this session, but the TOML file could not be updated.");
        }
        RefreshSortPanelLabel();
        D2RL::LogInfoF(Context,
            "OfflineCharacterOrder 0.9.29: custom order moved character=%s direction=%d",
            displayedRows[static_cast<std::size_t>(selectedRow)].name.c_str(), action->direction);
    } catch (...) {
        Context->LogError("OfflineCharacterOrder: custom move failed safely; the current list was not changed further.");
    }
}

auto ScheduleCustomOrderMove(CustomOrderInputAction& action) noexcept -> bool {
    if (Context == nullptr || UiThreadService == nullptr
            || UiThreadService->runOnUiThread == nullptr) {
        return false;
    }
    return UiThreadService->runOnUiThread(
        Context, CustomOrderMoveUiCallback, &action)
        == D2RL::Threads::Result::Success;
}

LRESULT CALLBACK CustomOrderKeyboardProc(int code, WPARAM virtualKey,
        LPARAM keyData) noexcept {
    const auto hook = CustomOrderKeyboardHook.load(std::memory_order_acquire);
    if (code < 0 || code != HC_ACTION) {
        return CallNextHookEx(hook, code, virtualKey, keyData);
    }

    std::size_t actionIndex{};
    if (virtualKey == static_cast<WPARAM>('W')) {
        actionIndex = 0;
    } else if (virtualKey == static_cast<WPARAM>('S')) {
        actionIndex = 1;
    } else {
        return CallNextHookEx(hook, code, virtualKey, keyData);
    }

    auto& action = CustomOrderInputActions[actionIndex];
    const bool keyUp = (static_cast<ULONG_PTR>(keyData)
        & (ULONG_PTR{1} << 31)) != 0;
    if (keyUp) {
        const bool handled = action.consumed.exchange(false,
            std::memory_order_acq_rel);
        action.pressed.store(false, std::memory_order_release);
        return handled ? 1 : CallNextHookEx(hook, code, virtualKey, keyData);
    }

    const bool controlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    if (!controlDown || !Operational.load(std::memory_order_acquire)
            || !OfflineSelectionActive.load(std::memory_order_acquire)
            || !CustomModeActive.load(std::memory_order_acquire)) {
        return CallNextHookEx(hook, code, virtualKey, keyData);
    }
    if (action.pressed.exchange(true, std::memory_order_acq_rel)) {
        return action.consumed.load(std::memory_order_acquire)
            ? 1 : CallNextHookEx(hook, code, virtualKey, keyData);
    }
    action.consumed.store(true, std::memory_order_release);
    if (!ScheduleCustomOrderMove(action)) {
        action.pressed.store(false, std::memory_order_release);
        action.consumed.store(false, std::memory_order_release);
        return CallNextHookEx(hook, code, virtualKey, keyData);
    }
    return 1;
}

auto InitializeCustomOrderKeyboardHook() noexcept -> bool {
    if (CustomOrderKeyboardHook.load(std::memory_order_acquire) != nullptr) {
        return true;
    }
    const auto hook = SetWindowsHookExW(WH_KEYBOARD,
        CustomOrderKeyboardProc, nullptr, GetCurrentThreadId());
    if (hook == nullptr) {
        if (Context != nullptr) {
            D2RL::LogWarnF(Context,
                "OfflineCharacterOrder: selector keyboard hook installation failed (error=%lu).",
                static_cast<unsigned long>(GetLastError()));
        }
        return false;
    }
    CustomOrderKeyboardHook.store(hook, std::memory_order_release);
    if (Context != nullptr) {
        Context->LogInfo("OfflineCharacterOrder: Ctrl+W/S selector keyboard hook installed.");
    }
    return true;
}

void ShutdownCustomOrderKeyboardHook() noexcept {
    const auto hook = CustomOrderKeyboardHook.exchange(nullptr,
        std::memory_order_acq_rel);
    if (hook != nullptr && !UnhookWindowsHookEx(hook) && Context != nullptr) {
        D2RL::LogWarnF(Context,
            "OfflineCharacterOrder: selector keyboard hook removal failed (error=%lu).",
            static_cast<unsigned long>(GetLastError()));
    }
    for (auto& action : CustomOrderInputActions) {
        action.pressed.store(false, std::memory_order_release);
        action.consumed.store(false, std::memory_order_release);
    }
}

void ScheduleSortPanelSync() noexcept;
void ResetScreenState() noexcept;
void __cdecl OnGameJoined(const D2RL::PluginContext* context,
        const D2RL::Lifecycle::GameplayEvent* event, void*) noexcept;

auto IsSortPanelMessage(std::string_view message) noexcept -> bool {
    return message == OfflineCharacterOrder::SortPanel::LocalId
        || message == OfflineCharacterOrder::SortPanel::QualifiedName;
}

auto IsBlockingOverlayPanel(std::string_view panelName) noexcept -> bool {
    return panelName == "SettingsPanel"
        || panelName == "OptionsPanel"
        || panelName == "GrailerInfoPanel"
        || panelName == "GrailerInfoPanel2"
        || panelName == "CreditsModal"
        || panelName == "OnlineGameSettingsPanel";
}

auto IsNamedOverlay(std::string_view panelName) noexcept -> bool {
    return IsBlockingOverlayPanel(panelName)
        || panelName == "CinematicsModal";
}

void RefreshSortPanelOverlaySuppression() noexcept {
    const bool stillSuppressed = !OpenUiOverlayPanels.empty();
    const bool wasSuppressed = SortPanelSuppressedByOverlay.exchange(
        stillSuppressed, std::memory_order_acq_rel);
    if (wasSuppressed && !stillSuppressed) ScheduleSortPanelSync();
}

void CloseSortPanelImmediately() noexcept {
    if (SortPanelService != nullptr
            && SortPanelRegistrationHandle != D2RL::Panels::InvalidHandle
            && SortPanelService->closePanel != nullptr) {
        (void)SortPanelService->closePanel(Context,
            SortPanelRegistrationHandle);
    }
}

void SuppressSortPanelForOverlay(std::string_view panelName) noexcept {
    if (panelName.empty() || IsSortPanelMessage(panelName)) return;
    try {
        const auto alreadyOpen = std::find(OpenUiOverlayPanels.begin(),
            OpenUiOverlayPanels.end(), panelName);
        if (alreadyOpen == OpenUiOverlayPanels.end()) {
            OpenUiOverlayPanels.emplace_back(panelName);
        }
    } catch (...) {
        // Stay suppressed if we cannot remember an overlay; reopening over it
        // is worse than requiring the selector to be re-entered.
    }
    SortPanelSuppressedByOverlay.store(true, std::memory_order_release);
    CloseSortPanelImmediately();
}

void ReleaseSortPanelOverlay(std::string_view panelName) noexcept {
    if (IsSortPanelMessage(panelName)) return;
    const auto opened = std::find(OpenUiOverlayPanels.begin(),
        OpenUiOverlayPanels.end(), panelName);
    if (!panelName.empty() && opened != OpenUiOverlayPanels.end()) {
        OpenUiOverlayPanels.erase(opened);
    } else {
        // Loader-owned panels may close through a private ID that differs
        // from the PanelManager open ID. A close still dismisses unknown ones.
        OpenUiOverlayPanels.erase(std::remove_if(OpenUiOverlayPanels.begin(),
            OpenUiOverlayPanels.end(), [](const std::string& name) {
                return !IsNamedOverlay(name);
            }), OpenUiOverlayPanels.end());
    }
    RefreshSortPanelOverlaySuppression();
}

void ReleaseUnknownSortPanelOverlays() noexcept {
    OpenUiOverlayPanels.erase(std::remove_if(OpenUiOverlayPanels.begin(),
        OpenUiOverlayPanels.end(), [](const std::string& name) {
            return !IsNamedOverlay(name);
        }), OpenUiOverlayPanels.end());
    RefreshSortPanelOverlaySuppression();
}

auto __cdecl OnSortPanelUiMessage(
        const D2RL::PluginContext* context,
        const D2RL::SharedEvents::UiMessageEvent* event,
        void*) noexcept -> D2RL::SharedEvents::UiMessageAction {
    if (context == nullptr || context != Context || event == nullptr
            || event->structSize < D2RL::SharedEvents::UiMessageEventRequiredSize
            || event->target == nullptr || event->command == nullptr
            || !Operational.load(std::memory_order_acquire)) {
        return D2RL::SharedEvents::UiMessageAction::Continue;
    }
    const std::string_view target(event->target);
    const std::string_view command(event->command);
    const std::string_view message = event->text != nullptr
        ? std::string_view(event->text) : std::string_view{};
    if (target == "PanelManager" && command == "OpenPanel") {
        if (IsSortPanelMessage(message)) {
            return D2RL::SharedEvents::UiMessageAction::Continue;
        }
        if (message == "OfflineCharacterOrderPreviousMode"
                || message == "OfflineCharacterOrderNextMode") {
            // The sort arrows also use PanelManager:OpenPanel messages.
        } else {
            if (!IsBlockingOverlayPanel(message)) {
                // Loader and mod panels can have private IDs. Catch an
                // unrecognized one when it opens over an already-visible
                // sorter, without mistaking selector startup for an overlay.
                bool sortPanelOpen{};
                if (OfflineSelectionActive.load(std::memory_order_acquire)
                        && IsSortPanelOpen(sortPanelOpen) && sortPanelOpen) {
                    D2RL::LogInfoF(Context,
                        "OfflineCharacterOrder 0.9.29: suppressing sort panel for external panel=%.*s",
                        static_cast<int>(message.size()), message.data());
                    SuppressSortPanelForOverlay(message);
                }
                return D2RL::SharedEvents::UiMessageAction::Continue;
            }
            // Keep the selector active beneath modal panels, but prevent its
            // refresh hook from immediately reopening our panel over them.
            SuppressSortPanelForOverlay(message);
            return D2RL::SharedEvents::UiMessageAction::Continue;
        }
    } else if (target == "PanelManager" && command == "ClosePanel") {
        ReleaseSortPanelOverlay(message);
        return D2RL::SharedEvents::UiMessageAction::Continue;
    } else if (target == "FrontEndNavigation") {
        if (command == "ToCinematics") {
            SuppressSortPanelForOverlay("CinematicsModal");
        } else if (command == "ToCharacterSelect"
                || command == "ToJoinCharacterSelect") {
            // Returning to character select is not a departure. The native
            // selector hook will reactivate us if the Offline list is shown.
            OpenUiOverlayPanels.clear();
            SortPanelSuppressedByOverlay.store(false,
                std::memory_order_release);
            ScheduleSortPanelSync();
        } else {
            // Navigation to character creation, Multiplayer, or another
            // frontend screen ends this selector session.
            ResetScreenState();
            CloseSortPanelImmediately();
        }
        return D2RL::SharedEvents::UiMessageAction::Continue;
    } else if (target == "Cinematics" && command == "Close") {
        ReleaseSortPanelOverlay("CinematicsModal");
        return D2RL::SharedEvents::UiMessageAction::Continue;
    } else if (command == "Close" || command == "Dismiss"
            || command == "Cancel" || command == "Back") {
        // Some loader/mod overlays use their own message target instead of
        // PanelManager:ClosePanel when the user leaves them.
        ReleaseUnknownSortPanelOverlays();
        return D2RL::SharedEvents::UiMessageAction::Continue;
    } else {
        return D2RL::SharedEvents::UiMessageAction::Continue;
    }

    const int direction = message == "OfflineCharacterOrderPreviousMode" ? -1
        : message == "OfflineCharacterOrderNextMode" ? 1 : 0;
    if (direction == 0) {
        return D2RL::SharedEvents::UiMessageAction::Continue;
    }
    bool panelOpen{};
    if (!IsSortPanelOpen(panelOpen) || !panelOpen) {
        return D2RL::SharedEvents::UiMessageAction::Continue;
    }
    CurrentMode = CycleSortMode(CurrentMode, direction);
    CustomModeActive.store(CurrentMode == SortMode::Custom,
        std::memory_order_release);
    D2RL::LogInfoF(Context,
        "OfflineCharacterOrder 0.9.29: sort arrow selected mode=%s direction=%d",
        ModeName(CurrentMode), direction);
    if (!PersistCurrentSortMode()) {
        Context->LogWarn("OfflineCharacterOrder 0.9.29: selected sort mode is active, but the config file could not be updated.");
    }
    SortModeApplyPending.store(true, std::memory_order_release);
    RefreshSortPanelLabel();
    ScheduleSortPanelSync();
    return D2RL::SharedEvents::UiMessageAction::Consume;
}

void __cdecl SortPanelUiCallback(const D2RL::PluginContext*, void*) noexcept {
    SortPanelSyncPending.store(false, std::memory_order_release);
    if (Context == nullptr || SortPanelRegistrationHandle
            == D2RL::Panels::InvalidHandle) {
        return;
    }
    void* const activePanel = ActiveOfflinePanelWidget;
    const bool shouldBeOpen = Operational.load(std::memory_order_acquire)
        && OfflineSelectionActive.load(std::memory_order_acquire)
        && !SortPanelSuppressedByOverlay.load(std::memory_order_acquire)
        && ActiveOfflinePanel(activePanel);
    bool isOpen{};
    if (!IsSortPanelOpen(isOpen)) return;
    if (shouldBeOpen && !isOpen) {
        const auto result = SortPanelService->openPanel(
            Context, SortPanelRegistrationHandle);
        if (result != D2RL::Panels::Result::Success) {
            D2RL::LogWarnF(Context,
                "OfflineCharacterOrder 0.9.29: sort panel could not be opened (result=%u).",
                static_cast<unsigned>(result));
            return;
        }
        isOpen = true;
    } else if (!shouldBeOpen && isOpen) {
        (void)SortPanelService->closePanel(Context, SortPanelRegistrationHandle);
        isOpen = false;
    }
    if (shouldBeOpen && isOpen) RefreshSortPanelLabel();

    if (SortModeApplyPending.exchange(false, std::memory_order_acq_rel)
            && shouldBeOpen) {
        std::uint64_t list{};
        if (ReadField(activePanel, CharacterSelectListOffset, list) && list != 0) {
            try {
                if (!UpdateRows(reinterpret_cast<void*>(list))) {
                    Context->LogWarn("OfflineCharacterOrder 0.9.29: selected sort mode could not be applied to the current character list.");
                }
            } catch (...) {
                Context->LogError("OfflineCharacterOrder 0.9.29: sort mode update failed; the current list was left unchanged.");
            }
        }
    }
}

void ScheduleSortPanelSync() noexcept {
    if (Context == nullptr || UiThreadService == nullptr
            || UiThreadService->runOnUiThread == nullptr
            || SortPanelRegistrationHandle == D2RL::Panels::InvalidHandle) {
        return;
    }
    bool expected = false;
    if (!SortPanelSyncPending.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        return;
    }
    const auto result = UiThreadService->runOnUiThread(
        Context, SortPanelUiCallback, nullptr);
    if (result != D2RL::Threads::Result::Success) {
        SortPanelSyncPending.store(false, std::memory_order_release);
        D2RL::LogWarnF(Context,
            "OfflineCharacterOrder 0.9.29: sort panel UI scheduling failed with result=%u.",
            static_cast<unsigned>(result));
    }
}

auto InitializeSortPanel(const D2RL::PluginContext* context) noexcept -> bool {
    if (context == nullptr) return false;
    if (context->QueryService(D2RL::ServiceId::Lifecycle,
            D2RL::LifecycleServiceV1Version, &GameplayLifecycleService)
            != D2RL::ServiceQueryResult::Success
            || !D2RL::HasLifecycleServiceV1Field(GameplayLifecycleService,
                D2RL::LifecycleServiceV1RequiredSize)
            || GameplayLifecycleService->registerGameplayEventListener == nullptr
            || GameplayLifecycleService->unregisterGameplayEventListener == nullptr) {
        GameplayLifecycleService = nullptr;
        context->LogWarn("OfflineCharacterOrder: LifecycleService is unavailable; the sort panel will not be shown because its game-entry transition cannot be tracked safely.");
        return false;
    }
    if (context->QueryService(D2RL::ServiceId::Resource,
            D2RL::ResourceServiceV1Version, &SortPanelResourceService)
            != D2RL::ServiceQueryResult::Success
            || !D2RL::HasResourceServiceV1Field(SortPanelResourceService,
                D2RL::ResourceServiceV1RequiredSize)
            || SortPanelResourceService->registerResource == nullptr
            || SortPanelResourceService->unregisterResource == nullptr) {
        context->LogWarn("OfflineCharacterOrder 0.9.29: ResourceService is unavailable; the sort buttons will not be shown.");
        return false;
    }
    if (context->QueryService(D2RL::ServiceId::Panel,
            D2RL::PanelServiceV1Version, &SortPanelService)
            != D2RL::ServiceQueryResult::Success
            || !D2RL::HasPanelServiceV1Field(SortPanelService,
                D2RL::PanelServiceV1RequiredSize)
            || SortPanelService->registerPanel == nullptr
            || SortPanelService->unregisterPanel == nullptr
            || SortPanelService->getPanelInfo == nullptr
            || SortPanelService->openPanel == nullptr
            || SortPanelService->closePanel == nullptr) {
        context->LogWarn("OfflineCharacterOrder 0.9.29: PanelService is unavailable; the sort buttons will not be shown.");
        return false;
    }
    if (context->QueryService(D2RL::ServiceId::SharedEvent,
            D2RL::SharedEventServiceV1Version, &SortPanelEventService)
            != D2RL::ServiceQueryResult::Success
            || !D2RL::HasSharedEventServiceV1Field(SortPanelEventService,
                D2RL::SharedEventServiceV1RequiredSize)
            || SortPanelEventService->registerUiMessageListener == nullptr
            || SortPanelEventService->unregisterUiMessageListener == nullptr) {
        context->LogWarn("OfflineCharacterOrder 0.9.29: SharedEventService is unavailable; the sort buttons will not be shown.");
        return false;
    }
    if (context->QueryService(D2RL::ServiceId::Widget,
            D2RL::WidgetServiceV1Version, &SortPanelWidgetService)
            != D2RL::ServiceQueryResult::Success
            || !D2RL::HasWidgetServiceV1Field(SortPanelWidgetService,
                D2RL::WidgetServiceV1RequiredSize)
            || SortPanelWidgetService->findPanel == nullptr
            || SortPanelWidgetService->findWidget == nullptr
            || SortPanelWidgetService->setWidgetVisible == nullptr) {
        SortPanelWidgetService = nullptr;
        context->LogWarn("OfflineCharacterOrder 0.9.29: WidgetService is unavailable; the mode label cannot be synchronized.");
        return false;
    }

    std::vector<std::uint8_t> leftArrow;
    std::vector<std::uint8_t> rightArrow;
    std::vector<std::uint8_t> modePlaque;
    if (!LoadEmbeddedResource(OFFLINE_CHARACTER_ORDER_LEFT_ARROW_RESOURCE_ID,
            leftArrow)
            || !LoadEmbeddedResource(OFFLINE_CHARACTER_ORDER_RIGHT_ARROW_RESOURCE_ID,
                rightArrow)
            || !LoadEmbeddedResource(OFFLINE_CHARACTER_ORDER_MODE_PLAQUE_RESOURCE_ID,
                modePlaque)) {
        context->LogWarn("OfflineCharacterOrder 0.9.29: embedded sort artwork could not be loaded.");
        return false;
    }
    if (!RegisterSortPanelResource(context,
            OfflineCharacterOrder::SortPanel::LayoutResourcePath,
            OfflineCharacterOrder::SortPanel::Layout,
            OfflineCharacterOrder::SortPanel::LayoutView.size(),
            SortPanelLayoutResourceHandle)
            || !RegisterSortPanelResource(context,
                OfflineCharacterOrder::SortPanel::LeftArrowResourcePath,
                leftArrow.data(), leftArrow.size(), SortPanelArtworkHandles[0])
            || !RegisterSortPanelResource(context,
                OfflineCharacterOrder::SortPanel::RightArrowResourcePath,
                rightArrow.data(), rightArrow.size(), SortPanelArtworkHandles[1])
            || !RegisterSortPanelResource(context,
                OfflineCharacterOrder::SortPanel::PlaqueResourcePath,
                modePlaque.data(), modePlaque.size(), SortPanelArtworkHandles[2])) {
        context->LogWarn("OfflineCharacterOrder 0.9.29: sort panel layout or artwork registration failed.");
        UnregisterSortPanelResources();
        return false;
    }

    const D2RL::Panels::PanelRegistration panel{
        .structSize = D2RL::Panels::PanelRegistrationSize,
        .flags = D2RL::Panels::PanelFlags::None,
        .localId = OfflineCharacterOrder::SortPanel::LocalId,
    };
    if (SortPanelService->registerPanel(context, &panel,
            &SortPanelRegistrationHandle) != D2RL::Panels::Result::Success
            || SortPanelRegistrationHandle == D2RL::Panels::InvalidHandle) {
        context->LogWarn("OfflineCharacterOrder 0.9.29: sort panel could not be registered.");
        UnregisterSortPanelResources();
        return false;
    }

    const D2RL::SharedEvents::UiMessageListener listener{
        .structSize = D2RL::SharedEvents::UiMessageListenerSize,
        .flags = 0,
        .priority = 10000,
        .reserved = 0,
        .callback = OnSortPanelUiMessage,
        .userData = nullptr,
    };
    if (SortPanelEventService->registerUiMessageListener(context, &listener,
            &SortPanelMessageHandle) != D2RL::SharedEvents::Result::Success
            || SortPanelMessageHandle == D2RL::SharedEvents::InvalidHandle) {
        context->LogWarn("OfflineCharacterOrder 0.9.29: sort panel arrow listener could not be registered.");
        (void)SortPanelService->unregisterPanel(context,
            SortPanelRegistrationHandle);
        SortPanelRegistrationHandle = D2RL::Panels::InvalidHandle;
        UnregisterSortPanelResources();
        return false;
    }

    const D2RL::Lifecycle::GameplayEventListener gameJoinedListener{
        .structSize = D2RL::Lifecycle::GameplayEventListenerSize,
        .flags = 0,
        .kind = D2RL::Lifecycle::GameplayEventKind::GameJoined,
        .reserved = 0,
        .callback = OnGameJoined,
        .userData = nullptr,
    };
    if (GameplayLifecycleService->registerGameplayEventListener(context,
            &gameJoinedListener, &GameJoinedListenerHandle)
            != D2RL::Lifecycle::Result::Success
            || GameJoinedListenerHandle == D2RL::Lifecycle::InvalidHandle) {
        context->LogWarn("OfflineCharacterOrder: game-entry listener could not be registered; the sort panel will not be shown.");
        (void)SortPanelEventService->unregisterUiMessageListener(
            context, SortPanelMessageHandle);
        SortPanelMessageHandle = D2RL::SharedEvents::InvalidHandle;
        (void)SortPanelService->unregisterPanel(context,
            SortPanelRegistrationHandle);
        SortPanelRegistrationHandle = D2RL::Panels::InvalidHandle;
        UnregisterSortPanelResources();
        SortPanelWidgetService = nullptr;
        SortPanelEventService = nullptr;
        SortPanelService = nullptr;
        SortPanelResourceService = nullptr;
        GameplayLifecycleService = nullptr;
        return false;
    }
    context->LogInfo("OfflineCharacterOrder 0.9.29: Offline sort arrows registered.");
    return true;
}

void ShutdownSortPanel() noexcept {
    SortPanelSyncPending.store(false, std::memory_order_release);
    SortModeApplyPending.store(false, std::memory_order_release);
    if (Context != nullptr && SortPanelEventService != nullptr
            && SortPanelMessageHandle != D2RL::SharedEvents::InvalidHandle
            && SortPanelEventService->unregisterUiMessageListener != nullptr) {
        (void)SortPanelEventService->unregisterUiMessageListener(
            Context, SortPanelMessageHandle);
    }
    if (Context != nullptr && GameplayLifecycleService != nullptr
            && GameJoinedListenerHandle != D2RL::Lifecycle::InvalidHandle
            && GameplayLifecycleService->unregisterGameplayEventListener != nullptr) {
        (void)GameplayLifecycleService->unregisterGameplayEventListener(
            Context, GameJoinedListenerHandle);
    }
    if (Context != nullptr && SortPanelService != nullptr
            && SortPanelRegistrationHandle != D2RL::Panels::InvalidHandle
            && SortPanelService->unregisterPanel != nullptr) {
        (void)SortPanelService->unregisterPanel(
            Context, SortPanelRegistrationHandle);
    }
    SortPanelMessageHandle = D2RL::SharedEvents::InvalidHandle;
    GameJoinedListenerHandle = D2RL::Lifecycle::InvalidHandle;
    SortPanelRegistrationHandle = D2RL::Panels::InvalidHandle;
    UnregisterSortPanelResources();
    SortPanelWidgetService = nullptr;
    SortPanelEventService = nullptr;
    SortPanelService = nullptr;
    SortPanelResourceService = nullptr;
    GameplayLifecycleService = nullptr;
}

void ScheduleDeferredUiSortUpdate() noexcept;
void ResetScreenState() noexcept;

void __cdecl DeferredUiSortCallback(
        const D2RL::PluginContext*, void*) noexcept {
    DeferredUiUpdatePending.store(false, std::memory_order_release);
    if (!Operational.load(std::memory_order_acquire)
            || DeferredUiUpdateBudget.load(std::memory_order_acquire) == 0) {
        return;
    }
    auto* const panel = ActiveOfflinePanelWidget;
    if (!ActiveOfflinePanel(panel)) {
        ResetScreenState();
        return;
    }
    std::uint64_t list{};
    if (!ReadField(panel, CharacterSelectListOffset, list) || list == 0) {
        DeferredUiUpdateBudget.store(0, std::memory_order_release);
        return;
    }
    const bool capture = !DeferredDiagnosticCapturedThisScreen.exchange(
        true, std::memory_order_acq_rel);
    if (capture) {
        LogRowSnapshot("deferred-before-update", reinterpret_cast<void*>(list));
    }
    try {
        if (!UpdateRows(reinterpret_cast<void*>(list))) {
            if (!ReportedRowAccessFailure.exchange(true, std::memory_order_acq_rel)
                    && Context != nullptr) {
                Context->LogWarn("OfflineCharacterOrder 0.9.29: deferred row layout validation failed; no changes were made in that update.");
            }
        }
        if (capture) {
            LogRowSnapshot("deferred-after-update", reinterpret_cast<void*>(list));
        }
    } catch (...) {
        Operational.store(false, std::memory_order_release);
        DeferredUiUpdateBudget.store(0, std::memory_order_release);
        if (Context != nullptr) {
            Context->LogError("OfflineCharacterOrder 0.9.29: deferred UI update failed; sorting was disabled for this session.");
        }
        return;
    }
    const auto remaining = DeferredUiUpdateBudget.fetch_sub(
        1, std::memory_order_acq_rel);
    if (remaining > 1) ScheduleDeferredUiSortUpdate();
}

void ScheduleDeferredUiSortUpdate() noexcept {
    if (Context == nullptr || UiThreadService == nullptr
            || UiThreadService->runOnUiThread == nullptr) {
        return;
    }
    bool expected = false;
    if (!DeferredUiUpdatePending.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        return;
    }
    const auto result = UiThreadService->runOnUiThread(
        Context, DeferredUiSortCallback, nullptr);
    if (result != D2RL::Threads::Result::Success) {
        DeferredUiUpdatePending.store(false, std::memory_order_release);
        DeferredUiUpdateBudget.store(0, std::memory_order_release);
        if (Context != nullptr) {
            D2RL::LogWarnF(Context,
                "OfflineCharacterOrder 0.9.29: UI-thread scheduling failed with result=%u.",
                static_cast<unsigned>(result));
        }
    }
}

void ResetCharacterListState(bool clearNativeOrder = true) noexcept {
    DatasetKnown = false;
    SelectionOffsetKnown = false;
    SelectionOffsetY = 0;
    LastSyncedSelectionWidget = nullptr;
    SelectedCharacterWidget = nullptr;
    SelectedCharacterName.clear();
    LastObservedNativeSelectedIndex = -2;
    if (clearNativeOrder) NativeOrderNames.clear();
    DatasetSignature.clear();
    SlotPositions.clear();
    CachedListWidget = nullptr;
}

void ResetScreenState() noexcept {
    const bool wasActive = OfflineSelectionActive.load(std::memory_order_acquire);
    OfflineSelectionActive.store(false, std::memory_order_release);
    OpenUiOverlayPanels.clear();
    SortPanelSuppressedByOverlay.store(false, std::memory_order_release);
    ShutdownCustomOrderKeyboardHook();
    ReportedRowAccessFailure.store(false, std::memory_order_release);
    DiagnosticCapturedThisScreen.store(false, std::memory_order_release);
    DeferredUpdatesStartedThisScreen.store(false, std::memory_order_release);
    DeferredDiagnosticCapturedThisScreen.store(false, std::memory_order_release);
    DeferredUiUpdateBudget.store(0, std::memory_order_release);
    SortModeApplyPending.store(false, std::memory_order_release);
    for (auto& action : CustomOrderInputActions) {
        action.pressed.store(false, std::memory_order_release);
        action.consumed.store(false, std::memory_order_release);
    }
    ActiveOfflinePanelWidget = nullptr;
    ResetCharacterListState();
    if (wasActive && Operational.load(std::memory_order_acquire)) {
        ScheduleSortPanelSync();
    }
}

void __cdecl OnGameJoined(const D2RL::PluginContext* context,
        const D2RL::Lifecycle::GameplayEvent* event, void*) noexcept {
    if (context == nullptr || context != Context || event == nullptr
            || !D2RL::Lifecycle::HasGameplayEventField(event,
                D2RL::Lifecycle::GameplayEventRequiredSize)
            || event->kind != D2RL::Lifecycle::GameplayEventKind::GameJoined) {
        return;
    }

    // This listener runs on the UI thread. Close synchronously so the selector
    // cannot remain visible for one more click while the game starts loading.
    OfflineSelectionActive.store(false, std::memory_order_release);
    ResetScreenState();
    if (SortPanelService != nullptr
            && SortPanelRegistrationHandle != D2RL::Panels::InvalidHandle
            && SortPanelService->closePanel != nullptr) {
        (void)SortPanelService->closePanel(Context,
            SortPanelRegistrationHandle);
    }
}

auto __fastcall CharacterSelectEventHook(void* panel, void* event) noexcept -> void* {
    const auto original = OriginalCharacterSelectEvent;
    if (!Operational.load(std::memory_order_acquire)) {
        return original != nullptr ? original(panel, event) : nullptr;
    }
    if (!ActiveOfflinePanel(panel)) {
        ResetScreenState();
        return original != nullptr ? original(panel, event) : nullptr;
    }
    const bool wasActive = OfflineSelectionActive.exchange(
        true, std::memory_order_acq_rel);
    ActiveOfflinePanelWidget = panel;
    (void)InitializeCustomOrderKeyboardHook();
    if (!wasActive) ScheduleSortPanelSync();
    std::uint64_t list{};
    if (!ReadField(panel, CharacterSelectListOffset, list) || list == 0) {
        return original != nullptr ? original(panel, event) : nullptr;
    }
    const bool datasetKnownBeforeEvent = DatasetKnown;
    const bool captureDiagnostic = !DiagnosticCapturedThisScreen.exchange(
        true, std::memory_order_acq_rel);
    if (captureDiagnostic) {
        LogRowSnapshot("before-sort", reinterpret_cast<void*>(list));
        std::vector<CharacterRow> diagnosticRows;
        void* diagnosticArray{};
        std::int32_t selectedIndex{};
        if (ReadRows(reinterpret_cast<void*>(list), diagnosticRows,
                diagnosticArray, selectedIndex)) {
            LogMatchingTextWidgets(panel, diagnosticRows, "before-sort");
        }
    }
    // D2R rebuilds this list during later panel events. Reapply the sort on
    // both sides of the native handler so hit testing always sees displayed rows.
    bool datasetChangedBeforeEvent = false;
    try {
        if (!UpdateRows(reinterpret_cast<void*>(list), &datasetChangedBeforeEvent)
                && !ReportedRowAccessFailure.exchange(true, std::memory_order_acq_rel)
                && Context != nullptr) {
            Context->LogWarn("OfflineCharacterOrder 0.9.29: row layout validation failed; no changes were made in that update.");
        }
    } catch (...) {
        Operational.store(false, std::memory_order_release);
        if (Context != nullptr) {
            Context->LogError("OfflineCharacterOrder 0.9.29: update failed before the native event; sorting was disabled for this session.");
        }
    }

    if (captureDiagnostic) {
        LogRowSnapshot("before-native-event", reinterpret_cast<void*>(list));
        std::vector<CharacterRow> diagnosticRows;
        void* diagnosticArray{};
        std::int32_t selectedIndex{};
        if (ReadRows(reinterpret_cast<void*>(list), diagnosticRows,
                diagnosticArray, selectedIndex)) {
            LogMatchingTextWidgets(panel, diagnosticRows, "after-sort");
        }
    }
    const auto originalResult = original != nullptr ? original(panel, event) : nullptr;

    std::vector<CharacterRow> rowsAfterEvent;
    void* arrayAfterEvent{};
    std::int32_t selectedIndexAfterEvent{-2};
    bool selectionChanged = false;
    bool datasetChangedAfterEvent = false;
    if (ReadRows(reinterpret_cast<void*>(list), rowsAfterEvent,
            arrayAfterEvent, selectedIndexAfterEvent)) {
        const auto signatureAfterEvent = WidgetSignature(rowsAfterEvent);
        datasetChangedAfterEvent = !DatasetKnown
            || CachedListWidget != reinterpret_cast<void*>(list)
            || signatureAfterEvent != DatasetSignature;
        if (datasetChangedAfterEvent && !datasetKnownBeforeEvent) {
            // If the first valid list snapshot is only available after the native
            // callback, seed selection from that snapshot. Later rebuilds are
            // handled below by resetting and recapturing the list mapping.
            if (selectedIndexAfterEvent >= 0
                    && static_cast<std::size_t>(selectedIndexAfterEvent)
                        < rowsAfterEvent.size()) {
                const auto& nativeSelection = rowsAfterEvent[
                    static_cast<std::size_t>(selectedIndexAfterEvent)];
                SelectedCharacterWidget = nativeSelection.widget;
                SelectedCharacterName = nativeSelection.name;
                D2RL::LogInfoF(Context,
                    "OfflineCharacterOrder 0.9.29: initial list adopted native selection=%s index=%d",
                    SelectedCharacterName.c_str(), selectedIndexAfterEvent);
            }
            selectionChanged = true;
            LastObservedNativeSelectedIndex = selectedIndexAfterEvent;
        } else if (!datasetChangedAfterEvent
                && selectedIndexAfterEvent != LastObservedNativeSelectedIndex) {
            if (selectedIndexAfterEvent >= 0
                    && static_cast<std::size_t>(selectedIndexAfterEvent)
                    < NativeOrderNames.size()) {
                const auto& selectedName = NativeOrderNames[
                    static_cast<std::size_t>(selectedIndexAfterEvent)];
                const auto selectedRow = std::find_if(rowsAfterEvent.begin(),
                    rowsAfterEvent.end(), [&selectedName](const auto& row) {
                        return EqualInsensitive(row.name, selectedName);
                    });
                if (selectedRow != rowsAfterEvent.end()) {
                    SelectedCharacterWidget = selectedRow->widget;
                    SelectedCharacterName = selectedRow->name;
                    selectionChanged = true;
                }
            }
            LastObservedNativeSelectedIndex = selectedIndexAfterEvent;
        }
    }
    (void)arrayAfterEvent;

    const bool datasetRebuilt = datasetChangedBeforeEvent
        || (datasetKnownBeforeEvent && datasetChangedAfterEvent);
    if (datasetRebuilt) {
        // A delete can alter the row set before this hook enters the native
        // handler. The handler then refreshes the list's index/click bindings.
        // Discard the old interaction mapping after it runs and recapture from
        // the rebuilt, currently displayed rows. Preserve Most Recent's stable
        // order separately so every other mode keeps its own click mapping.
        ResetCharacterListState(false);
        selectionChanged = false;
        D2RL::LogInfoF(Context,
            "OfflineCharacterOrder 0.9.29: character list rebuilt; refreshing row layout while preserving native index order (rows=%zu)",
            rowsAfterEvent.size());
        DeferredUiUpdateBudget.store(8, std::memory_order_release);
        ScheduleDeferredUiSortUpdate();
    }

    try {
        if (!datasetRebuilt && selectionChanged
                && Operational.load(std::memory_order_acquire)
                && !UpdateRows(reinterpret_cast<void*>(list))
                && !ReportedRowAccessFailure.exchange(true, std::memory_order_acq_rel)
                && Context != nullptr) {
            Context->LogWarn("OfflineCharacterOrder 0.9.29: row layout validation failed after the native event; no changes were made in that update.");
        }
    } catch (...) {
        Operational.store(false, std::memory_order_release);
        DeferredUiUpdateBudget.store(0, std::memory_order_release);
        if (Context != nullptr) {
            Context->LogError("OfflineCharacterOrder 0.9.29: update failed after the native event; sorting was disabled for this session.");
        }
    }

    if (!DeferredUpdatesStartedThisScreen.exchange(true,
            std::memory_order_acq_rel)) {
        DeferredUiUpdateBudget.store(8, std::memory_order_release);
        ScheduleDeferredUiSortUpdate();
    }
    if (captureDiagnostic) {
        LogRowSnapshot("after-native-event", reinterpret_cast<void*>(list));
        std::vector<CharacterRow> diagnosticRows;
        void* diagnosticArray{};
        std::int32_t selectedIndex{};
        if (ReadRows(reinterpret_cast<void*>(list), diagnosticRows,
                diagnosticArray, selectedIndex)) {
            LogMatchingTextWidgets(panel, diagnosticRows, "after-native-event");
        }
    }
    ScheduleSortPanelSync();
    return originalResult;
}

void ResetRuntime() noexcept {
    Operational.store(false, std::memory_order_release);
    ShutdownCustomOrderKeyboardHook();
    ShutdownSortPanel();
    ResetScreenState();
    DeferredUiUpdatePending.store(false, std::memory_order_release);
    SortPanelSyncPending.store(false, std::memory_order_release);
    SortModeApplyPending.store(false, std::memory_order_release);
    DeferredUiUpdateBudget.store(0, std::memory_order_release);
    UiThreadService = nullptr;
    CurrentMode = SortMode::NameAscending;
    CustomModeActive.store(false, std::memory_order_release);
    CustomOrderNames.clear();
    OriginalCharacterSelectEvent = nullptr;
}

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept
        -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(
        const D2RL::PluginContext* context) noexcept -> bool {
    ResetRuntime();
    if (!D2RL::HasContext(context)
            || context->apiVersion != D2RL_PLUGIN_API_VERSION) {
        return false;
    }
    Context = context;
    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);
    if (Base == nullptr || !ReadConfiguration()) return false;
    const auto threadServiceResult = context->QueryService(
        D2RL::ServiceId::Thread, D2RL::ThreadServiceV1Version, &UiThreadService);
    if (threadServiceResult != D2RL::ServiceQueryResult::Success
            || UiThreadService == nullptr
            || UiThreadService->serviceVersion != D2RL::ThreadServiceV1Version
            || UiThreadService->serviceSize < D2RL::ThreadServiceV1Size
            || UiThreadService->runOnUiThread == nullptr) {
        UiThreadService = nullptr;
        Context->LogWarn("OfflineCharacterOrder 0.9.29: UI-thread service unavailable; deferred refresh will be skipped.");
    }
    if (std::memcmp(Base + CharacterSelectEventRva,
            CharacterSelectEventExpected.data(), CharacterSelectEventExpected.size()) != 0) {
        Context->LogError("OfflineCharacterOrder 0.9.29: unsupported D2R build; the 3.3.93847 CharacterSelectPanel event fingerprint did not match.");
        return false;
    }
    if (!Context->InstallInlineHook(CharacterSelectEventRva,
            CharacterSelectEventExpected.data(),
            static_cast<std::uint32_t>(CharacterSelectEventExpected.size()),
            CharacterSelectEventHook, &OriginalCharacterSelectEvent)
            || OriginalCharacterSelectEvent == nullptr) {
        Context->LogError("OfflineCharacterOrder 0.9.29: CharacterSelectPanel event hook could not be installed.");
        return false;
    }
    (void)InitializeSortPanel(Context);
    Operational.store(true, std::memory_order_release);
    D2RL::LogInfoF(Context,
        "Offline Character Order 0.9.29 active; mode=%s, changes apply only to the Offline character list.",
        ModeName(CurrentMode));
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    Operational.store(false, std::memory_order_release);
    ShutdownCustomOrderKeyboardHook();
    ShutdownSortPanel();
    ResetScreenState();
    DeferredUiUpdatePending.store(false, std::memory_order_release);
    SortPanelSyncPending.store(false, std::memory_order_release);
    SortModeApplyPending.store(false, std::memory_order_release);
    DeferredUiUpdateBudget.store(0, std::memory_order_release);
    UiThreadService = nullptr;
    OriginalCharacterSelectEvent = nullptr;
    Base = nullptr;
    Context = nullptr;
}
