#pragma once
#include "SWindow.h"

class UControlPanelWindow;
class USceneWindow;

class SControlPanel :
    public SWindow
{
public:
    SControlPanel();
    virtual ~SControlPanel();

    void Initialize();
    virtual void OnRender() override;
    virtual void OnUpdate(float deltaSecond) override;

private:
    UControlPanelWindow* ControlPanelWidget;
    USceneWindow* SceneWindow;
};

