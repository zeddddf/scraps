#pragma once

#include <windows.h>
#include "ui/D2dShell.h"
#include "ui/settings/SettingsTypes.h"

namespace clipd2d::ui {

class FlexNode;

class ISettingsHost {
public:
    virtual ~ISettingsHost() = default;

    virtual D2dShell& GetShell() = 0;
    virtual void AddHitRegion(const HitRegion& region) = 0;
    virtual void RegisterFieldRect(FieldId field, const RECT& rect) = 0;
    virtual void RegisterSelectRect(SelectField field, const RECT& rect) = 0;

    virtual FieldId GetActiveField() const = 0;
    virtual SelectField GetActiveSelectField() const = 0;
    virtual const DataStats& GetDataStats() const = 0;

    virtual int DipToPx(float dip) const = 0;
    virtual float GetScrollY() const = 0;
    virtual std::wstring GetSelectLabel(SelectField field) const = 0;

    // B3 粘性节标题：面板阶段只入队，宿主在行绘制完成后统一画在最上层，
    // 否则滚动的卡片会盖住钉在视口顶部的标题。
    virtual void QueueStickyTitle(const FlexNode* node, const wchar_t* text) = 0;
};

}  // namespace clipd2d::ui
