#pragma once
#include "PostProcessing.h"

class FDOFSetupPass final : public IPostProcessPass
{
public:
    virtual void Execute(const FPostProcessModifier& M, FSceneView* View, D3D11RHI* RHIDevice) override;
};
