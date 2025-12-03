#include "pch.h"
#include "MyCar.h"
#include "SkeletalMeshComponent.h"
#include "World.h"
#include "Source/Runtime/Engine/Physics/PhysScene.h"
#include "Source/Runtime/InputCore/InputManager.h"
#include <PxPhysicsAPI.h>

// PhysX Vehicle SDK includes
#include "vehicle/PxVehicleSDK.h"
#include "vehicle/PxVehicleUtil.h"
#include "vehicle/PxVehicleDrive4W.h"
#include "vehicle/PxVehicleUtilSetup.h"

using namespace physx;

// Helper function to create vehicle wheel data
static void SetupWheelsSimulationData(
    float WheelMass, float WheelRadius, float WheelWidth, float WheelMOI,
    const FVector& ChassisCMOffset, int32 NumWheels, PxVehicleWheelsSimData* WheelsSimData)
{
    // Setup wheels - Z-up 좌표계에서 바퀴 위치 설정
    PxVec3 WheelCentreOffsets[PX_MAX_NB_WHEELS];
    
    // Front wheels - 앞쪽이 +Y 방향 (Z-up 왼손좌표계)
    WheelCentreOffsets[PxVehicleDrive4WWheelOrder::eFRONT_LEFT] = 
        PxVec3(-1.0f, 1.5f, -0.5f);  // 좌측 앞바퀴: X-, Y+, Z-
    WheelCentreOffsets[PxVehicleDrive4WWheelOrder::eFRONT_RIGHT] = 
        PxVec3(1.0f, 1.5f, -0.5f);   // 우측 앞바퀴: X+, Y+, Z-
    
    // Rear wheels - 뒤쪽이 -Y 방향 (Z-up 왼손좌표계)
    WheelCentreOffsets[PxVehicleDrive4WWheelOrder::eREAR_LEFT] = 
        PxVec3(-1.0f, -1.5f, -0.5f); // 좌측 뒷바퀴: X-, Y-, Z-
    WheelCentreOffsets[PxVehicleDrive4WWheelOrder::eREAR_RIGHT] = 
        PxVec3(1.0f, -1.5f, -0.5f);  // 우측 뒷바퀴: X+, Y-, Z-

    // Setup wheel simulation data with proper default values
    for (PxU32 i = 0; i < static_cast<PxU32>(NumWheels); i++)
    {
        // Create wheel data with proper defaults
        PxVehicleWheelData wheelData;
        wheelData.mRadius = WheelRadius;
        wheelData.mWidth = WheelWidth;
        wheelData.mMass = WheelMass;
        wheelData.mMOI = WheelMOI;
        wheelData.mMaxSteer = (i < 2) ? PxPi * 0.3333f : 0.0f; // Front wheels can steer
        wheelData.mMaxHandBrakeTorque = 4000.0f;
        wheelData.mMaxBrakeTorque = 1500.0f;
        WheelsSimData->setWheelData(i, wheelData);
        
        // Create tire data with PROPER friction values for better traction
        PxVehicleTireData tireData;
        tireData.mType = 0;
        // 타이어 마찰력 개선 - 기본 마찰 곡선만 설정
        // 마찰 곡선 개선 - 슬립에 따른 마찰력 변화
        tireData.mFrictionVsSlipGraph[0][0] = 0.0f;   // 슬립 0%에서 마찰력 100%
        tireData.mFrictionVsSlipGraph[0][1] = 1.0f;   
        tireData.mFrictionVsSlipGraph[1][0] = 0.1f;   // 슬립 10%에서 마찰력 100% 유지
        tireData.mFrictionVsSlipGraph[1][1] = 1.0f;   
        tireData.mFrictionVsSlipGraph[2][0] = 1.0f;   // 슬립 100%에서 마찰력 80%
        tireData.mFrictionVsSlipGraph[2][1] = 0.8f;
        WheelsSimData->setTireData(i, tireData);
        
        // Create suspension data with proper defaults
        PxVehicleSuspensionData suspData;
        /*suspData.mMaxCompression = 0.3f;
        suspData.mMaxDroop = 0.1f;
        suspData.mSpringStrength = 35000.0f;
        suspData.mSpringDamperRate = 4500.0f;*/
        suspData.mMaxCompression = 0.6f;  // 늘림
        suspData.mMaxDroop = 0.3f;        // 늘림  
        suspData.mSpringStrength = 25000.0f; // 약간 줄임
        suspData.mSpringDamperRate = 3500.0f; // 약간 줄임
        // NOTE: mSprungMass is set automatically by setChassisMass() - DO NOT set manually
        WheelsSimData->setSuspensionData(i, suspData);
        
        WheelsSimData->setWheelShapeMapping(i, i + 1);
        
        // Set wheel center offset
        WheelsSimData->setWheelCentreOffset(i, WheelCentreOffsets[i]);
        
        // Z-up 좌표계에서 서스펜션 이동 방향은 -Z (아래쪽)
        WheelsSimData->setSuspTravelDirection(i, PxVec3(0, 0, -1));
        
        //// Set suspension force application point offset - Z-up에서 Z축 위쪽으로 오프셋
        //WheelsSimData->setSuspForceAppPointOffset(i, PxVec3(WheelCentreOffsets[i].x, WheelCentreOffsets[i].y, WheelCentreOffsets[i].z + 0.3f));
        //
        //// Set tire force application point offset - 바퀴 중심에서 약간 아래쪽으로 설정
        //WheelsSimData->setTireForceAppPointOffset(i, PxVec3(WheelCentreOffsets[i].x, WheelCentreOffsets[i].y, WheelCentreOffsets[i].z - 0.1f));

        // Set suspension force application point offset - Z-up에서 Z축 위쪽으로 오프셋
        WheelsSimData->setSuspForceAppPointOffset(i, PxVec3(WheelCentreOffsets[i].x, WheelCentreOffsets[i].y, -0.3f));

        // Set tire force application point offset - 바퀴 중심에서 약간 아래쪽으로 설정
        WheelsSimData->setTireForceAppPointOffset(i, PxVec3(WheelCentreOffsets[i].x, WheelCentreOffsets[i].y, -0.3f));
    }
}

AMyCar::AMyCar()
{
    // Create skeletal mesh component for the car
    VehicleMesh = CreateDefaultSubobject<USkeletalMeshComponent>("VehicleMesh");
    SetRootComponent(VehicleMesh);
    
    // Initialize wheel query results
    WheelQueryResults.SetNum(PX_MAX_NB_WHEELS);
}

AMyCar::~AMyCar()
{
    CleanupVehiclePhysics();
}

void AMyCar::BeginPlay()
{
    Super::BeginPlay();
    
    // Initialize PhysX vehicle
    InitializeVehiclePhysics();
    
    UE_LOG("[MyCarComponent] Vehicle initialized successfully");
}

void AMyCar::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    
    if (!VehicleDrive4W)
        return;
    
    // Process keyboard input
    ProcessKeyboardInput(DeltaTime);
    
    // Update vehicle physics
    UpdateVehiclePhysics(DeltaTime);
}

void AMyCar::EndPlay()
{
    CleanupVehiclePhysics();
    Super::EndPlay();
}

void AMyCar::DuplicateSubObjects()
{
    Super::DuplicateSubObjects();
	// Duplicate any sub-objects if necessary

    // Find skeletal mesh component (always exists)
    for (UActorComponent* Component : OwnedComponents)
    {
        if (auto* Comp = Cast<USkeletalMeshComponent>(Component))
        {
            VehicleMesh = Comp;
            break;
        }
    }
}

void AMyCar::InitializeVehiclePhysics()
{
    UWorld* World = GetWorld();
    if (!World || !World->GetPhysScene())
    {
        UE_LOG("[MyCarComponent] Failed to initialize: No PhysScene");
        return;
    }
    
    FPhysScene* PhysScene = World->GetPhysScene();
    PxScene* PxScenePtr = PhysScene->GetScene();
    PxPhysics* Physics = PhysScene->GetPhysics();
    PxMaterial* Material = PhysScene->GetDefaultMaterial();
	//PxMaterial* Material = Physics->createMaterial(0.5f, 0.5f, 0.6f); // 동적 마찰계수, 정적 마찰계수, 반발계수
    
    if (!PxScenePtr || !Physics || !Material)
    {
        UE_LOG("[MyCarComponent] Failed to initialize: Invalid PhysX scene");
        return;
    }

    // Initialize PhysX Vehicle SDK
    if (!PxInitVehicleSDK(*Physics))
    {
        UE_LOG("[MyCarComponent] Failed to initialize PhysX Vehicle SDK");
        return;
    }
    
    // Set basis vectors (Z-up coordinate system)
    PxVehicleSetBasisVectors(PxVec3(0, 0, 1), PxVec3(1, 0, 0));
    
    // Set update mode
    PxVehicleSetUpdateMode(PxVehicleUpdateMode::eVELOCITY_CHANGE);

    // 레이캐스트 결과 버퍼 초기화
    RaycastResults.SetNum(NumWheels);
    RaycastHitBuffer.SetNum(NumWheels);
    
    // Create batch query for vehicle raycasts
    PxBatchQueryDesc batchQueryDesc(NumWheels, 0, 0);
    batchQueryDesc.queryMemory.userRaycastResultBuffer = RaycastResults.GetData();
    batchQueryDesc.queryMemory.userRaycastTouchBuffer = RaycastHitBuffer.GetData();
    batchQueryDesc.queryMemory.raycastTouchBufferSize = NumWheels;
    batchQueryDesc.preFilterShader = nullptr;
    BatchQuery = PxScenePtr->createBatchQuery(batchQueryDesc);

    // Friction Pairs 생성 - 더 많은 타이어 타입과 표면 타입 지원
    const PxU32 numTireTypes = 1;
    const PxU32 numSurfaceTypes = 1;
    
    PxVehicleDrivableSurfaceType surfaceTypes[1];
    surfaceTypes[0].mType = 0;

    const PxMaterial* surfaceMaterials[1];
    surfaceMaterials[0] = Material;

    FrictionPairs = PxVehicleDrivableSurfaceToTireFrictionPairs::allocate(numTireTypes, numSurfaceTypes);
    FrictionPairs->setup(numTireTypes, numSurfaceTypes, surfaceMaterials, surfaceTypes);

    // 마찰력 강화 - 타이어와 도로 간 마찰력 증가
    for (PxU32 i = 0; i < numTireTypes; i++)
    {
        for (PxU32 j = 0; j < numSurfaceTypes; j++)
        {
            // 마찰력을 1.5로 증가시켜 더 나은 트랙션 제공
            FrictionPairs->setTypePairFriction(i, j, 1.5f);
        }
    }
    
    // Create the vehicle
    CreateVehicle4W();
    
    UE_LOG("[MyCarComponent] PhysX Vehicle SDK initialized");
}

void AMyCar::CreateVehicle4W()
{
    UWorld* World = GetWorld();
    if (!World || !World->GetPhysScene())
        return;
    
    FPhysScene* PhysScene = World->GetPhysScene();
    PxPhysics* Physics = PhysScene->GetPhysics();
    PxScene* PxScenePtr = PhysScene->GetScene();
    PxMaterial* Material = PhysScene->GetDefaultMaterial();
    
    if (!Physics || !PxScenePtr || !Material)
        return;
    
    // Calculate MOI for chassis and wheels - Z-up 좌표계에 맞게 수정
    const PxVec3 ChassisHalfExtents(ChassisDimensions.X * 0.5f, 
                                    ChassisDimensions.Y * 0.5f, 
                                    ChassisDimensions.Z * 0.5f);
    
    // Z-up 좌표계에서의 관성 모멘트 계산
    const PxVec3 ChassisMOI(
        // X축 회전 (롤): Y²+Z² 성분 - Z-up에서 X축은 차량의 롤 회전
        (ChassisHalfExtents.y * ChassisHalfExtents.y + ChassisHalfExtents.z * ChassisHalfExtents.z) * ChassisMass / 12.0f,
        // Y축 회전 (피치): X²+Z² 성분 - Z-up에서 Y축은 차량의 피치 회전
        (ChassisHalfExtents.x * ChassisHalfExtents.x + ChassisHalfExtents.z * ChassisHalfExtents.z) * ChassisMass / 12.0f,
        // Z축 회전 (요): X²+Y² 성분 - Z-up에서 Z축은 차량의 요 회전 (좌우 흔들림 감소를 위해 약간 작게)
        (ChassisHalfExtents.x * ChassisHalfExtents.x + ChassisHalfExtents.y * ChassisHalfExtents.y) * 0.8f * ChassisMass / 12.0f
    );
    
    // Z-up 좌표계에서 샤시 무게중심 오프셋: Z축이 위쪽이므로 차체 중심에서 약간 아래쪽
    const PxVec3 ChassisCMOffset(0.0f, 0.25f, -ChassisDimensions.Z * 0.5f + 0.65f);
    const FVector ChassisCMOffsetFVector(ChassisCMOffset.x, ChassisCMOffset.y, ChassisCMOffset.z);
    
    // Create rigid body
    FTransform WorldTransform = GetActorTransform();
    PxTransform StartPose(
        PxVec3(WorldTransform.Translation.X, WorldTransform.Translation.Y, WorldTransform.Translation.Z),
        PxQuat(WorldTransform.Rotation.X, WorldTransform.Rotation.Y, 
               WorldTransform.Rotation.Z, WorldTransform.Rotation.W)
    );
    
    PxRigidDynamic* VehicleActor = Physics->createRigidDynamic(StartPose);
    
    if (!VehicleActor)
    {
        UE_LOG("[MyCarComponent] Failed to create vehicle actor");
        return;
    }
    
    // Create chassis shape
    PxShape* ChassisShape = PxRigidActorExt::createExclusiveShape(
        *VehicleActor,
        PxBoxGeometry(ChassisHalfExtents),
        *Material
    );
    
    if (!ChassisShape)
    {
        VehicleActor->release();
        UE_LOG("[MyCarComponent] Failed to create chassis shape");
        return;
    }
    
    // Create wheel shapes
    for (int32 i = 0; i < NumWheels; i++)
    {
        PxShape* WheelShape = PxRigidActorExt::createExclusiveShape(
            *VehicleActor,
            PxSphereGeometry(WheelRadius),
            *Material
        );
        
        if (!WheelShape)
        {
            VehicleActor->release();
            UE_LOG("[MyCarComponent] Failed to create wheel shape %d", i);
            return;
        }
    }
    
    // Set mass and inertia
    VehicleActor->setMass(ChassisMass);
    VehicleActor->setMassSpaceInertiaTensor(ChassisMOI);
    VehicleActor->setCMassLocalPose(PxTransform(ChassisCMOffset, PxQuat(PxIdentity)));
    
    // Create vehicle drive data
    PxVehicleWheelsSimData* WheelsSimData = PxVehicleWheelsSimData::allocate(NumWheels);
    
    const float WheelMass = 20.0f;
    const float WheelMOI = 0.5f * WheelMass * WheelRadius * WheelRadius;
    
    SetupWheelsSimulationData(WheelMass, WheelRadius, WheelWidth, WheelMOI, 
                               ChassisCMOffsetFVector, NumWheels, WheelsSimData);
    
    // CRITICAL: Set the chassis mass distribution for suspension sprung masses
    WheelsSimData->setChassisMass(ChassisMass);
    
    // Create drive simulation data
    PxVehicleDriveSimData4W DriveSimData;
    
    // Setup Ackermann geometry - Z-up 좌표계에 맞게 조정
    PxVehicleAckermannGeometryData AckermannData;
    AckermannData.mAccuracy = 1.0f;
    AckermannData.mFrontWidth = 2.0f; // 앞바퀴 간격 (X축 방향)
    AckermannData.mRearWidth = 2.0f;  // 뒷바퀴 간격 (X축 방향)
    AckermannData.mAxleSeparation = 3.0f; // 전후축 거리 (Y축 방향)
    DriveSimData.setAckermannGeometryData(AckermannData);
    
    // Setup differential (4WD)
    PxVehicleDifferential4WData Diff;
    Diff.mType = PxVehicleDifferential4WData::eDIFF_TYPE_LS_4WD;
    DriveSimData.setDiffData(Diff);
    
    // Setup engine with torque curve using PxFixedSizeLookupTable
    PxVehicleEngineData Engine;
    Engine.mPeakTorque = 800.0f;  // 토크 증가
    Engine.mMaxOmega = 800.0f;    // 최대 RPM 증가
    Engine.mDampingRateFullThrottle = 0.15f;  // 풀스로틀 시 댐핑
    Engine.mDampingRateZeroThrottleClutchEngaged = 2.0f;  // 클러치 연결 시 댐핑
    Engine.mDampingRateZeroThrottleClutchDisengaged = 0.35f;  // 클러치 분리 시 댐핑
    
    // Define torque curve points (normalized RPM -> normalized torque)
    // This creates a realistic engine torque curve
    PxFixedSizeLookupTable<8>& TorqueCurve = Engine.mTorqueCurve;
    TorqueCurve.clear(); // Clear existing pairs first
    TorqueCurve.addPair(0.0f,  0.8f);  // Idle
    TorqueCurve.addPair(0.33f, 1.0f);  // Peak torque at 33% RPM
    TorqueCurve.addPair(0.5f,  0.95f); // Mid-range
    TorqueCurve.addPair(0.7f,  0.85f); // High RPM falloff
    TorqueCurve.addPair(1.0f,  0.7f);  // Max RPM
    
    DriveSimData.setEngineData(Engine);
    
    // Setup gears with better gear ratios
    PxVehicleGearsData Gears;
    Gears.mSwitchTime = 0.3f;  // 변속 시간 단축
    Gears.mNbRatios = 6;  // 기어 수 증가
    Gears.mRatios[PxVehicleGearsData::eREVERSE] = -4.0f;
    Gears.mRatios[PxVehicleGearsData::eNEUTRAL] = 0.0f;
    Gears.mRatios[PxVehicleGearsData::eFIRST] = 4.0f;
    Gears.mRatios[PxVehicleGearsData::eSECOND] = 2.0f;
    Gears.mRatios[PxVehicleGearsData::eTHIRD] = 1.5f;
    Gears.mRatios[PxVehicleGearsData::eFOURTH] = 1.1f;
    Gears.mRatios[PxVehicleGearsData::eFIFTH] = 1.0f;
    Gears.mFinalRatio = 4.0f;  // 최종 기어비 증가로 더 강한 출력
    DriveSimData.setGearsData(Gears);
    
    // Setup auto-box with improved settings
    PxVehicleAutoBoxData AutoBox;
    AutoBox.mUpRatios[PxVehicleGearsData::eFIRST] = 0.65f;
    AutoBox.mUpRatios[PxVehicleGearsData::eSECOND] = 0.65f;
    AutoBox.mUpRatios[PxVehicleGearsData::eTHIRD] = 0.65f;
    AutoBox.mUpRatios[PxVehicleGearsData::eFOURTH] = 0.65f;
    AutoBox.mDownRatios[PxVehicleGearsData::eFIRST] = 0.5f;
    AutoBox.mDownRatios[PxVehicleGearsData::eSECOND] = 0.5f;
    AutoBox.mDownRatios[PxVehicleGearsData::eTHIRD] = 0.5f;
    AutoBox.mDownRatios[PxVehicleGearsData::eFOURTH] = 0.5f;
    AutoBox.mDownRatios[PxVehicleGearsData::eFIFTH] = 0.5f;
    DriveSimData.setAutoBoxData(AutoBox);
    
    // Create the vehicle - 마지막 매개변수를 0으로 수정
    VehicleDrive4W = PxVehicleDrive4W::allocate(NumWheels);
    
    // Null 체크 추가
    if (!VehicleDrive4W)
    {
        UE_LOG("[MyCarComponent] Failed to allocate vehicle");
        VehicleActor->release();
        WheelsSimData->free();
        return;
    }
    
    // Setup 호출 - 4륜차의 경우 마지막 매개변수는 0이어야 함
    VehicleDrive4W->setup(Physics, VehicleActor, *WheelsSimData, DriveSimData, 0);
    
    // Add actor to scene
    PxScenePtr->addActor(*VehicleActor);
    
    // Set to rest state and first gear
    VehicleDrive4W->setToRestState();
    VehicleDrive4W->mDriveDynData.forceGearChange(PxVehicleGearsData::eFIRST);
    VehicleDrive4W->mDriveDynData.setUseAutoGears(true);
    
    // 초기 엔진 회전수 설정
    VehicleDrive4W->mDriveDynData.setEngineRotationSpeed(100.0f);
    
    // Free simulation data
    WheelsSimData->free();
    
    UE_LOG("[MyCarComponent] Vehicle 4W created successfully");
}

void AMyCar::ProcessKeyboardInput(float DeltaTime)
{
    UInputManager& InputManager = UInputManager::GetInstance();
    
    // Reset inputs
    float TargetThrottle = 0.0f;
    float TargetBrake = 0.0f;
    float TargetSteer = 0.0f;
    float TargetHandbrake = 0.0f;
    
    // Arrow key controls
    if (InputManager.IsKeyDown(VK_UP))
    {
        TargetThrottle = 1.0f;
    }
    if (InputManager.IsKeyDown(VK_DOWN))
    {
        TargetBrake = 1.0f;
    }
    if (InputManager.IsKeyDown(VK_LEFT))
    {
        TargetSteer = -1.0f;
    }
    if (InputManager.IsKeyDown(VK_RIGHT))
    {
        TargetSteer = 1.0f;
    }
    if (InputManager.IsKeyDown(VK_SPACE))
    {
        TargetHandbrake = 1.0f;
    }
    
    // Smooth input transitions
    if (TargetThrottle > CurrentThrottleInput)
    {
        CurrentThrottleInput = FMath::Min(CurrentThrottleInput + ThrottleRiseRate * DeltaTime, TargetThrottle);
    }
    else
    {
        CurrentThrottleInput = FMath::Max(CurrentThrottleInput - ThrottleFallRate * DeltaTime, TargetThrottle);
    }
    
    if (TargetBrake > CurrentBrakeInput)
    {
        CurrentBrakeInput = FMath::Min(CurrentBrakeInput + ThrottleRiseRate * DeltaTime, TargetBrake);
    }
    else
    {
        CurrentBrakeInput = FMath::Max(CurrentBrakeInput - ThrottleFallRate * DeltaTime, TargetBrake);
    }
    
    if (TargetSteer > CurrentSteerInput)
    {
        CurrentSteerInput = FMath::Min(CurrentSteerInput + SteerRiseRate * DeltaTime, TargetSteer);
    }
    else if (TargetSteer < CurrentSteerInput)
    {
        CurrentSteerInput = FMath::Max(CurrentSteerInput - SteerRiseRate * DeltaTime, TargetSteer);
    }
    else
    {
        // Return to center
        if (CurrentSteerInput > 0.0f)
        {
            CurrentSteerInput = FMath::Max(CurrentSteerInput - SteerFallRate * DeltaTime, 0.0f);
        }
        else if (CurrentSteerInput < 0.0f)
        {
            CurrentSteerInput = FMath::Min(CurrentSteerInput + SteerFallRate * DeltaTime, 0.0f);
        }
    }
    
    CurrentHandbrakeInput = TargetHandbrake;
    
    // Apply to vehicle
    VehicleInput.AnalogAccel = CurrentThrottleInput;
    VehicleInput.AnalogBrake = CurrentBrakeInput;
    VehicleInput.AnalogSteer = CurrentSteerInput;
    VehicleInput.AnalogHandbrake = CurrentHandbrakeInput;
}

void AMyCar::UpdateVehiclePhysics(float DeltaTime)
{
    if (!VehicleDrive4W || !BatchQuery)
        return;
    
    UWorld* World = GetWorld();
    if (!World || !World->GetPhysScene())
        return;
    
    FPhysScene* PhysScene = World->GetPhysScene();
    PxScene* PxScenePtr = PhysScene->GetScene();
    
    if (!PxScenePtr)
        return;
    
    // 1. Apply input
    VehicleDrive4W->mDriveDynData.setAnalogInput(
        PxVehicleDrive4WControl::eANALOG_INPUT_ACCEL, 
        VehicleInput.AnalogAccel);
    VehicleDrive4W->mDriveDynData.setAnalogInput(
        PxVehicleDrive4WControl::eANALOG_INPUT_BRAKE, 
        VehicleInput.AnalogBrake);
    VehicleDrive4W->mDriveDynData.setAnalogInput(
        PxVehicleDrive4WControl::eANALOG_INPUT_STEER_LEFT, 
        FMath::Max(0.0f, -VehicleInput.AnalogSteer));
    VehicleDrive4W->mDriveDynData.setAnalogInput(
        PxVehicleDrive4WControl::eANALOG_INPUT_STEER_RIGHT, 
        FMath::Max(0.0f, VehicleInput.AnalogSteer));
    VehicleDrive4W->mDriveDynData.setAnalogInput(
        PxVehicleDrive4WControl::eANALOG_INPUT_HANDBRAKE, 
        VehicleInput.AnalogHandbrake);
    
    // 2. Setup raycast batch using PxVehicleSuspensionRaycasts helper
    PxVehicleWheels* vehicles[1] = {VehicleDrive4W};
    PxRaycastQueryResult* raycastResults = RaycastResults.GetData();
    const PxU32 raycastResultsSize = RaycastResults.Num();
    
    // Perform suspension raycasts
    PxVehicleSuspensionRaycasts(
        BatchQuery,           // The batch query
        1,                    // Number of vehicles
        vehicles,             // Array of vehicle pointers
        raycastResultsSize,   // Size of raycast results array
        raycastResults        // Output raycast results
    );
    
    // 3. Prepare wheel query results from raycast hits
    PxWheelQueryResult wheelQueryResults[PX_MAX_NB_WHEELS];
    const PxU32 numWheels = VehicleDrive4W->mWheelsSimData.getNbWheels();

    // 레이캐스트 결과 디버깅 추가
    for (PxU32 i = 0; i < numWheels; i++)
    {
        UE_LOG("[MyCarComponent] Wheel %d: Raycast hits = %d", i, raycastResults[i].getNbAnyHits());

        if (raycastResults[i].getNbAnyHits() > 0)
        {
            const PxRaycastHit& hit = raycastResults[i].getAnyHit(0);
            UE_LOG("[MyCarComponent] Wheel %d: Hit distance = %.3f", i, hit.distance);
            UE_LOG("[MyCarComponent] Wheel %d: Hit position = (%.2f, %.2f, %.2f)",
                i, hit.position.x, hit.position.y, hit.position.z);
        }
        else
        {
            UE_LOG("[MyCarComponent] Wheel %d: NO HITS!", i);
        }
    }
    
    PxVehicleWheelQueryResult vehicleQueryResults[1] = {
        {wheelQueryResults, numWheels}
    };
    
    // 4. Update vehicle physics
    const PxVec3 grav = PxScenePtr->getGravity();
    
    const float FixedTimeStep = 1.0f / 60.0f;
    PxVehicleUpdates(FixedTimeStep, grav, *FrictionPairs, 1, vehicles, vehicleQueryResults);

    // Check if vehicle is in air
	bool bIsSleeping = VehicleDrive4W->getRigidDynamicActor()->isSleeping();

    bIsVehicleInAir = PxVehicleIsInAir(vehicleQueryResults[0]);
    
    // 5. Sync transform
    if (VehicleDrive4W->getRigidDynamicActor())
    {
		UE_LOG("[MyCarComponent] Vehicle Sleeping: %s", bIsSleeping ? "Yes" : "No");
		UE_LOG("[MyCarComponent] Vehicle In Air: %s", bIsVehicleInAir ? "Yes" : "No");
		UE_LOG("[MyCarComponent] Vehicle Speed: %.2f m/s", VehicleDrive4W->computeForwardSpeed());
        UE_LOG("[MyCarComponent] Vehicle Position: X=%.2f Y=%.2f Z=%.2f", 
            VehicleDrive4W->getRigidDynamicActor()->getGlobalPose().p.x,
            VehicleDrive4W->getRigidDynamicActor()->getGlobalPose().p.y,
			VehicleDrive4W->getRigidDynamicActor()->getGlobalPose().p.z);

        PxTransform PxTrans = VehicleDrive4W->getRigidDynamicActor()->getGlobalPose();
        
        FTransform NewTransform;
        NewTransform.Translation = FVector(PxTrans.p.x, PxTrans.p.y, PxTrans.p.z);
        NewTransform.Rotation = FQuat(PxTrans.q.x, PxTrans.q.y, PxTrans.q.z, PxTrans.q.w);
        NewTransform.Scale3D = FVector(1, 1, 1);
        
        SetActorTransform(NewTransform);
    }
}

void AMyCar::CleanupVehiclePhysics()
{
    if (VehicleDrive4W)
    {
        // Remove from scene
        if (VehicleDrive4W->getRigidDynamicActor())
        {
            UWorld* World = GetWorld();
            if (World && World->GetPhysScene())
            {
                FPhysScene* PhysScene = World->GetPhysScene();
                PxScene* PxScenePtr = PhysScene->GetScene();
                
                if (PxScenePtr)
                {
                    PxScenePtr->removeActor(*VehicleDrive4W->getRigidDynamicActor());
                }
            }
        }
        
        // Free vehicle
        VehicleDrive4W->free();
        VehicleDrive4W = nullptr;
    }
    
    if (BatchQuery)
    {
        BatchQuery->release();
        BatchQuery = nullptr;
    }
    
    if (FrictionPairs)
    {
        FrictionPairs->release();
        FrictionPairs = nullptr;
    }
    
    // Cleanup PhysX Vehicle SDK
    //PxCloseVehicleSDK();
    
    UE_LOG("[MyCarComponent] Vehicle physics cleaned up");
}

void AMyCar::ApplyThrottle(float Value)
{
    VehicleInput.AnalogAccel = FMath::Clamp(Value, 0.0f, 1.0f);
}

void AMyCar::ApplySteering(float Value)
{
    VehicleInput.AnalogSteer = FMath::Clamp(Value, -1.0f, 1.0f);
}

void AMyCar::ApplyBrake(float Value)
{
    VehicleInput.AnalogBrake = FMath::Clamp(Value, 0.0f, 1.0f);
}

void AMyCar::ApplyHandbrake(float Value)
{
    VehicleInput.AnalogHandbrake = FMath::Clamp(Value, 0.0f, 1.0f);
}
