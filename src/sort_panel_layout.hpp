#pragma once

#include <string>

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

struct LayoutSettings {
    double anchorX{0.845};
    double anchorY{0.0};
    int plaqueX{14};
    int plaqueY{0};
    int plaqueWidth{72};
    int plaqueHeight{14};
    int leftArrowX{-36};
    int leftArrowY{7};
    int leftArrowWidth{0};
    int leftArrowHeight{0};
    int rightArrowX{379};
    int rightArrowY{7};
    int rightArrowWidth{0};
    int rightArrowHeight{0};
    int labelX{160};
    int labelY{28};
    int labelWidth{72};
    int labelHeight{14};
    std::string labelFontFace{};
    int labelFontSize{0};
};

inline constexpr char LayoutTemplate[] = R"json({
  "type": "Panel",
  "name": "offline-character-order/SortModes",
  "fields": {
    "priority": 9004,
    "anchor": { "x": @anchor_x@, "y": @anchor_y@ },
    "rect": { "x": 0, "y": 0, "width": 100, "height": 18 },
    "defaultWidget": "PreviousModeButton"
  },
  "children": [
    {
      "type": "ImageWidget",
      "name": "ModePlaque",
      "fields": {
        "rect": { "x": @plaque_x@, "y": @plaque_y@, "width": @plaque_width@, "height": @plaque_height@ },
        "filename": "d2rloader/offline-character-order/sort-mode-plaque"
      }
    },
    {
      "type": "ButtonWidget",
      "name": "PreviousModeButton",
      "fields": {
        "rect": { "x": @left_arrow_x@, "y": @left_arrow_y@@left_arrow_size@ },
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
        "rect": { "x": @right_arrow_x@, "y": @right_arrow_y@@right_arrow_size@ },
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
        "rect": { "x": @label_x@, "y": @label_y@, "width": @label_width@, "height": @label_height@ },
        "text": "MOST RECENT",
        "visible": true,
        "style": {
          "fontColor": "$FontColorGoldYellow",
          @font_face_field@
          "pointSize": @point_size@,
          "alignment": { "h": "center", "v": "center" },
          "dropShadow": "$DefaultDropShadow"
        }
      }
    },
    {
      "type": "TextBoxWidget",
      "name": "LevelDescendingLabel",
      "fields": {
        "rect": { "x": @label_x@, "y": @label_y@, "width": @label_width@, "height": @label_height@ },
        "text": "LEVEL HIGH TO LOW",
        "visible": false,
        "style": {
          "fontColor": "$FontColorGoldYellow",
          @font_face_field@
          "pointSize": @point_size@,
          "alignment": { "h": "center", "v": "center" },
          "dropShadow": "$DefaultDropShadow"
        }
      }
    },
    {
      "type": "TextBoxWidget",
      "name": "LevelAscendingLabel",
      "fields": {
        "rect": { "x": @label_x@, "y": @label_y@, "width": @label_width@, "height": @label_height@ },
        "text": "LEVEL LOW TO HIGH",
        "visible": false,
        "style": {
          "fontColor": "$FontColorGoldYellow",
          @font_face_field@
          "pointSize": @point_size@,
          "alignment": { "h": "center", "v": "center" },
          "dropShadow": "$DefaultDropShadow"
        }
      }
    },
    {
      "type": "TextBoxWidget",
      "name": "NameAscendingLabel",
      "fields": {
        "rect": { "x": @label_x@, "y": @label_y@, "width": @label_width@, "height": @label_height@ },
        "text": "A TO Z",
        "visible": false,
        "style": {
          "fontColor": "$FontColorGoldYellow",
          @font_face_field@
          "pointSize": @point_size@,
          "alignment": { "h": "center", "v": "center" },
          "dropShadow": "$DefaultDropShadow"
        }
      }
    },
    {
      "type": "TextBoxWidget",
      "name": "NameDescendingLabel",
      "fields": {
        "rect": { "x": @label_x@, "y": @label_y@, "width": @label_width@, "height": @label_height@ },
        "text": "Z TO A",
        "visible": false,
        "style": {
          "fontColor": "$FontColorGoldYellow",
          @font_face_field@
          "pointSize": @point_size@,
          "alignment": { "h": "center", "v": "center" },
          "dropShadow": "$DefaultDropShadow"
        }
      }
    },
    {
      "type": "TextBoxWidget",
      "name": "ClassOrderLabel",
      "fields": {
        "rect": { "x": @label_x@, "y": @label_y@, "width": @label_width@, "height": @label_height@ },
        "text": "CLASS",
        "visible": false,
        "style": {
          "fontColor": "$FontColorGoldYellow",
          @font_face_field@
          "pointSize": @point_size@,
          "alignment": { "h": "center", "v": "center" },
          "dropShadow": "$DefaultDropShadow"
        }
      }
    },
    {
      "type": "TextBoxWidget",
      "name": "CustomLabel",
      "fields": {
        "rect": { "x": @label_x@, "y": @label_y@, "width": @label_width@, "height": @label_height@ },
        "text": "CUSTOM",
        "visible": false,
        "style": {
          "fontColor": "$FontColorGoldYellow",
          @font_face_field@
          "pointSize": @point_size@,
          "alignment": { "h": "center", "v": "center" },
          "dropShadow": "$DefaultDropShadow"
        }
      }
    }
  ]
})json";

} // namespace OfflineCharacterOrder::SortPanel
