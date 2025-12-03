#pragma once
#include "Pawn.h"
#include "AMyCar.generated.h"

// Forward declarations
class USkeletalMeshComponent;
class FPhysScene;
namespace physx {
    class PxVehicleDrive4W;
    class PxVehicleWheels;
    struct PxVehicleWheelQueryResult;
    struct PxWheelQueryResult;
    struct PxRaycastHit;
    class PxVehicleDrivableSurfaceToTireFrictionPairs;
    class PxBatchQuery;
}

// Vehicle input data structure
struct FVehicleInputData
{
    float AnalogAccel = 0.0f;
    float AnalogBrake = 0.0f;
    float AnalogSteer = 0.0f;
    float AnalogHandbrake = 0.0f;

    void Reset()
    {
        AnalogAccel = 0.0f;
        AnalogBrake = 0.0f;
        AnalogSteer = 0.0f;
        AnalogHandbrake = 0.0f;
    }
};

// Vehicle wheel setup data
struct FVehicleWheelSetup
{
    float WheelRadius = 0.5f;
    float WheelWidth = 0.4f;
    float WheelMass = 20.0f;
    FVector WheelOffset = FVector::Zero();
};

UCLASS(DisplayName="자동차 컴포넌트", Description="화살표키로 제어 가능한 차량 컴포넌트")
class AMyCar : public APawn
{
public:
    GENERATED_REFLECTION_BODY()
    
    AMyCar();
    virtual ~AMyCar() override;

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual void EndPlay() override;

    void DuplicateSubObjects() override;
protected:
    // Physics initialization
    void InitializeVehiclePhysics();
    void CreateVehicle4W();
    void UpdateVehiclePhysics(float DeltaTime);
    void CleanupVehiclePhysics();

    // Input processing
    void ProcessKeyboardInput(float DeltaTime);

    // Vehicle control
    void ApplyThrottle(float Value);
    void ApplySteering(float Value);
    void ApplyBrake(float Value);
    void ApplyHandbrake(float Value);

protected:
    USkeletalMeshComponent* VehicleMesh = nullptr;

    // PhysX vehicle data
    physx::PxVehicleDrive4W* VehicleDrive4W = nullptr;
    physx::PxVehicleDrivableSurfaceToTireFrictionPairs* FrictionPairs = nullptr;
    physx::PxBatchQuery* BatchQuery = nullptr;

    // 레이캐스트 결과 버퍼
    TArray<physx::PxRaycastQueryResult> RaycastResults;
    TArray<physx::PxRaycastHit> RaycastHitBuffer;

    // Vehicle input
    FVehicleInputData VehicleInput;
    
    // Vehicle parameters
    UPROPERTY(EditAnywhere, Category = "Vehicle")
    float ChassisMass = 1500.0f;
    
    UPROPERTY(EditAnywhere, Category = "Vehicle")
    FVector ChassisDimensions = FVector(2.5f, 2.0f, 5.0f);
    
    UPROPERTY(EditAnywhere, Category = "Vehicle")
    float WheelRadius = 0.5f;
    
    UPROPERTY(EditAnywhere, Category = "Vehicle")
    float WheelWidth = 0.4f;
    
    UPROPERTY(EditAnywhere, Category = "Vehicle")
    int32 NumWheels = 4;
    
    UPROPERTY(EditAnywhere, Category = "Vehicle")
    float MaxSteerAngle = 45.0f;
    
    UPROPERTY(EditAnywhere, Category = "Vehicle")
    float MaxSpeed = 100.0f;
    
    // Input smoothing
    UPROPERTY(EditAnywhere, Category = "Vehicle|Input")
    float SteerRiseRate = 2.5f;
    
    UPROPERTY(EditAnywhere, Category = "Vehicle|Input")
    float SteerFallRate = 5.0f;
    
    UPROPERTY(EditAnywhere, Category = "Vehicle|Input")
    float ThrottleRiseRate = 6.0f;
    
    UPROPERTY(EditAnywhere, Category = "Vehicle|Input")
    float ThrottleFallRate = 10.0f;

    // State tracking
    bool bIsVehicleInAir = false;
    float CurrentSteerInput = 0.0f;
    float CurrentThrottleInput = 0.0f;
    float CurrentBrakeInput = 0.0f;
    float CurrentHandbrakeInput = 0.0f;

    // Wheel query results buffer
    TArray<physx::PxWheelQueryResult> WheelQueryResults;
};
