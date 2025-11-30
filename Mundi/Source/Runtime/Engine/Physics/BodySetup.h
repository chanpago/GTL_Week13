#include "BodySetupCore.h"
#include "FKShapeElem.h"

struct FKSphereElem;
struct FKBoxElem;
struct FKSphylElem;
struct FKConvexElem;

struct FKAggregateGeom
{
    TArray<FKSphereElem> SphereElements;
    TArray<FKBoxElem>    BoxElements;
    TArray<FKSphylElem>  SphylElements;
    TArray<FKConvexElem> ConvexElements;

    void Clear();
    void Serialize(const bool bInIsLoading, JSON& InOutHandle);
};

class UPhysicalMaterial;
class UBodySetup : public UBodySetupCore
{
    DECLARE_CLASS(UBodySetup, UBodySetupCore)
public:
    UBodySetup();
	~UBodySetup() override;

    FKAggregateGeom AggGeom;             // 이 Body가 가진 Primitive Collision 모음

    float Mass = 10.0f;                
    float LinearDamping = 0.01f;  // 위치의 시간 당 변화량
    float AngularDamping = 0.05f; // 회전의 시간 당 변화량

    UPhysicalMaterial* PhysMaterial = nullptr;

    // Collision Setting
    bool bSimulatePhysics = true; // false일 시 Static
    bool bEnableGravity = true;   // 위가 false면 의미없음

    bool bCachedDataDirty = true;

    void AddSphere(const FKSphereElem& Elem);
    void AddBox(const FKBoxElem& Elem);
    void AddSphyl(const FKSphylElem& Elem);

    void BuildCachedData();
    void Serialize(const bool bInIsLoading, JSON& InOutHandle) override;
};