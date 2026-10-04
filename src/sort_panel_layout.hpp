#pragma once

#include <string_view>

namespace OfflineCharacterOrder::SortPanel {

inline constexpr char LocalId[] = "SortModes";
inline constexpr char QualifiedName[] = "offline-character-order/SortModes";
inline constexpr char LayoutResourcePath[] =
    "data/global/ui/layouts/offline-character-order/SortModeshd.json";
inline constexpr char LeftArrowResourcePath[] =
    "data/hd/global/ui/d2rloader/offline-character-order/sort-arrow-left.sprite";
inline constexpr char RightArrowResourcePath[] =
    "data/hd/global/ui/d2rloader/offline-character-order/sort-arrow-right.sprite";
inline constexpr char PlaqueResourcePath[] =
    "data/hd/global/ui/d2rloader/offline-character-order/sort-mode-plaque.sprite";

inline constexpr char Layout[] = R"json({
  "type": "Panel",
  "name": "offline-character-order/SortModes",
  "fields": {
    "priority": 9004,
    "anchor": { "x": 0.841, "y": 0.0 },
    "rect": { "x": 0, "y": 0, "width": 100, "height": 18 },
    "defaultWidget": "PreviousModeButton"
  },
  "children": [
    {
      "type": "ImageWidget",
      "name": "ModePlaque",
      "fields": {
        "rect": { "x": 14, "y": 2, "width": 72, "height": 14 },
        "filename": "d2rloader/offline-character-order/sort-mode-plaque"
      }
    },
    {
      "type": "ButtonWidget",
      "name": "PreviousModeButton",
      "fields": {
        "rect": { "x": -84, "y": -8 },
        "filename": "d2rloader/offline-character-order/sort-arrow-left",
        "onClickMessage": "PanelManager:OpenPanel:OfflineCharacterOrderPreviousMode",
        "disabledFrame": 1,
        "pressedFrame": 2,
        "hoveredFrame": 3
      }
    },
    {
      "type": "ButtonWidget",
      "name": "NextModeButton",
      "fields": {
        "rect": { "x": 365, "y": -8 },
        "filename": "d2rloader/offline-character-order/sort-arrow-right",
        "onClickMessage": "PanelManager:OpenPanel:OfflineCharacterOrderNextMode",
        "disabledFrame": 1,
        "pressedFrame": 2,
        "hoveredFrame": 3
      }
    },
    {
      "type": "TextBoxWidget",
      "name": "MostRecentLabel",
      "fields": {
        "rect": { "x": 160, "y": 30, "width": 72, "height": 14 },
        "text": "MOST RECENT",
        "visible": true,
        "style": {
          "fontColor": "$FontColorGoldYellow",
          "pointSize": "$SmallPanelFontSize",
          "alignment": { "h": "center", "v": "center" },
          "dropShadow": "$DefaultDropShadow"
        }
      }
    },
    {
      "type": "TextBoxWidget",
      "name": "LevelDescendingLabel",
      "fields": {
        "rect": { "x": 160, "y": 30, "width": 72, "height": 14 },
        "text": "LEVEL HIGH TO LOW",
        "visible": false,
        "style": {
          "fontColor": "$FontColorGoldYellow",
          "pointSize": "$SmallPanelFontSize",
          "alignment": { "h": "center", "v": "center" },
          "dropShadow": "$DefaultDropShadow"
        }
      }
    },
    {
      "type": "TextBoxWidget",
      "name": "LevelAscendingLabel",
      "fields": {
        "rect": { "x": 160, "y": 30, "width": 72, "height": 14 },
        "text": "LEVEL LOW TO HIGH",
        "visible": false,
        "style": {
          "fontColor": "$FontColorGoldYellow",
          "pointSize": "$SmallPanelFontSize",
          "alignment": { "h": "center", "v": "center" },
          "dropShadow": "$DefaultDropShadow"
        }
      }
    },
    {
      "type": "TextBoxWidget",
      "name": "NameAscendingLabel",
      "fields": {
        "rect": { "x": 160, "y": 30, "width": 72, "height": 14 },
        "text": "A TO Z",
        "visible": false,
        "style": {
          "fontColor": "$FontColorGoldYellow",
          "pointSize": "$SmallPanelFontSize",
          "alignment": { "h": "center", "v": "center" },
          "dropShadow": "$DefaultDropShadow"
        }
      }
    },
    {
      "type": "TextBoxWidget",
      "name": "NameDescendingLabel",
      "fields": {
        "rect": { "x": 160, "y": 30, "width": 72, "height": 14 },
        "text": "Z TO A",
        "visible": false,
        "style": {
          "fontColor": "$FontColorGoldYellow",
          "pointSize": "$SmallPanelFontSize",
          "alignment": { "h": "center", "v": "center" },
          "dropShadow": "$DefaultDropShadow"
        }
      }
    },
    {
      "type": "TextBoxWidget",
      "name": "ClassOrderLabel",
      "fields": {
        "rect": { "x": 160, "y": 30, "width": 72, "height": 14 },
        "text": "CLASS",
        "visible": false,
        "style": {
          "fontColor": "$FontColorGoldYellow",
          "pointSize": "$SmallPanelFontSize",
          "alignment": { "h": "center", "v": "center" },
          "dropShadow": "$DefaultDropShadow"
        }
      }
    },
    {
      "type": "TextBoxWidget",
      "name": "CustomLabel",
      "fields": {
        "rect": { "x": 160, "y": 30, "width": 72, "height": 14 },
        "text": "CUSTOM",
        "visible": false,
        "style": {
          "fontColor": "$FontColorGoldYellow",
          "pointSize": "$SmallPanelFontSize",
          "alignment": { "h": "center", "v": "center" },
          "dropShadow": "$DefaultDropShadow"
        }
      }
    }
  ]
})json";

inline constexpr std::string_view LayoutView{
    Layout,
    sizeof(Layout) - 1U,
};

} // namespace OfflineCharacterOrder::SortPanel
