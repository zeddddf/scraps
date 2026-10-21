#pragma once

#include <windows.h>
#include "ui/D2dShell.h"
#include "ui/settings/SettingsTypes.h"

namespace clipd2d::ui {

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
    virtual const std::wstring& GetSearchText() const = 0;
    
    virtual int DipToPx(float dip) const = 0;
    virtual float GetScrollY() const = 0;
    virtual std::wstring GetSelectLabel(SelectField field) const = 0;
};

}  // namespace clipd2d::ui
