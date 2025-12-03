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
// Helper function to create vehicle wheel data
static void SetupWheelsSimulationData(
    float WheelMass, float WheelRadius, float WheelWidth, float WheelMOI,
    const FVector& ChassisCMOffset, int32 NumWheels, PxVehicleWheelsSimData* WheelsSimData, float InChassisMass)
{
    // Setup wheels - Z-up 좌표계에서 바퀴 위치 설정
    PxVec3 WheelCentreOffsets[PX_MAX_NB_WHEELS];

    // Front wheels - 앞쪽이 +Y 방향 (Z-up 왼손좌표계)
    WheelCentreOffsets[PxVehicleDrive4WWheelOrder::eFRONT_LEFT] =
        PxVec3(-2.0f, 1.5f, 0.4f);  // 좌측 앞바퀴: X-, Y+, Z-
    WheelCentreOffsets[PxVehicleDrive4WWheelOrder::eFRONT_RIGHT] =
        PxVec3(2.0f, 1.5f, 0.4f);   // 우측 앞바퀴: X+, Y+, Z-

    // Rear wheels - 뒤쪽이 -Y 방향 (Z-up 왼손좌표계)
    WheelCentreOffsets[PxVehicleDrive4WWheelOrder::eREAR_LEFT] =
        PxVec3(-2.0f, -1.5f, 0.4f); // 좌측 뒷바퀴: X-, Y-, Z-
    WheelCentreOffsets[PxVehicleDrive4WWheelOrder::eREAR_RIGHT] =
        PxVec3(2.0f, -1.5f, 0.4f);  // 우측 뒷바퀴: X+, Y-, Z-

    // CRITICAL: Get chassis mass and calculate sprung mass per wheel
    const float ChassisMass = InChassisMass;
    const float SprungMassPerWheel = ChassisMass / static_cast<float>(NumWheels);

    UE_LOG("[SetupWheelsSimulationData] Chassis mass: %.2f, Sprung mass per wheel: %.2f", ChassisMass, SprungMassPerWheel);

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

        // Create suspension data with MANUAL sprung mass setting
        PxVehicleSuspensionData suspData;
        suspData.mMaxCompression = 0.6f;
        suspData.mMaxDroop = 0.3f;
        suspData.mSpringStrength = 25000.0f;
        suspData.mSpringDamperRate = 3500.0f;

        // CRITICAL: Manually set the sprung mass since setChassisMass() isn't working properly
        suspData.mSprungMass = SprungMassPerWheel;

        UE_LOG("[SetupWheelsSimulationData] Wheel %d: Setting sprung mass to %.2f", i, suspData.mSprungMass);

        WheelsSimData->setSuspensionData(i, suspData);

        WheelsSimData->setWheelShapeMapping(i, i + 1);

        // Set wheel center offset
        WheelsSimData->setWheelCentreOffset(i, WheelCentreOffsets[i]);

        // Z-up 좌표계에서 서스펜션 이동 방향은 -Z (아래쪽)
        WheelsSimData->setSuspTravelDirection(i, PxVec3(0, 0, -1));

        // Set suspension force application point offset - Z-up에서 Z축 위쪽으로 오프셋
        WheelsSimData->setSuspForceAppPointOffset(i, PxVec3(WheelCentreOffsets[i].x, WheelCentreOffsets[i].y, -0.3f));

        // Set tire force application point offset - 바퀴 중심에서 약간 아래쪽으로 설정
        WheelsSimData->setTireForceAppPointOffset(i, PxVec3(WheelCentreOffsets[i].x, WheelCentreOffsets[i].y, -0.3f));

        // CRITICAL: Set scene query filter data to prevent vehicle from hitting itself
        // 차량 서스펜션 레이캐스트용 필터 데이터 설정
        PxFilterData suspensionFilterData;
        suspensionFilterData.word0 = 0;    // 레이캐스트 자체는 특별한 그룹 없음
        suspensionFilterData.word1 = ~SURFACE_TYPE_VEHICLE;   // 차량 표면은 제외
        suspensionFilterData.word2 = 0;
        suspensionFilterData.word3 = 0;    // 레이캐스트는 표면 타입 없음
        WheelsSimData->setSceneQueryFilterData(i, suspensionFilterData);
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
    
    // Create batch query for vehicle raycasts with vehicle pre-filter
    PxBatchQueryDesc batchQueryDesc(NumWheels, 0, 0);
    batchQueryDesc.queryMemory.userRaycastResultBuffer = RaycastResults.GetData();
    batchQueryDesc.queryMemory.userRaycastTouchBuffer = RaycastHitBuffer.GetData();
    batchQueryDesc.queryMemory.raycastTouchBufferSize = NumWheels;
    batchQueryDesc.preFilterShader = VehicleWheelRaycastPreFilter;  // 차량용 pre-filter 사용
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
    
    UE_LOG("[MyCarComponent] PhysX Vehicle SDK initialized with vehicle surface filtering");
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

    // CRITICAL: Set vehicle surface filter data for chassis
    PxFilterData chassisFilterData;
    SetupVehicleSurface(chassisFilterData, true); // true = chassis
    ChassisShape->setSimulationFilterData(chassisFilterData);
    ChassisShape->setQueryFilterData(chassisFilterData);

    // Create wheel shapes with proper filtering
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

        // CRITICAL: Set vehicle surface filter data for wheels
        PxFilterData wheelFilterData;
        SetupVehicleSurface(wheelFilterData, false); // false = wheel
        WheelShape->setSimulationFilterData(wheelFilterData);
        WheelShape->setQueryFilterData(wheelFilterData);
    }

    // Set mass and inertia
    VehicleActor->setMass(ChassisMass);
    VehicleActor->setMassSpaceInertiaTensor(ChassisMOI);
    VehicleActor->setCMassLocalPose(PxTransform(ChassisCMOffset, PxQuat(PxIdentity)));

    // Create vehicle drive data
    PxVehicleWheelsSimData* WheelsSimData = PxVehicleWheelsSimData::allocate(NumWheels);

    // CRITICAL: Validate and set chassis mass
    UE_LOG("[MyCarComponent] Setting chassis mass: %.2f", ChassisMass);

    if (ChassisMass <= 0.0f)
    {
        UE_LOG("[MyCarComponent] ERROR: Invalid chassis mass: %.2f", ChassisMass);
        VehicleActor->release();
        WheelsSimData->free();
        return;
    }

    // Set the chassis mass FIRST
    WheelsSimData->setChassisMass(ChassisMass);

    // Verify chassis mass was set correctly
    const float VerifyMass = ChassisMass;
    UE_LOG("[MyCarComponent] Verified chassis mass: %.2f", VerifyMass);

    const float WheelMass = 20.0f;
    const float WheelMOI = 0.5f * WheelMass * WheelRadius * WheelRadius;

    // Setup wheels simulation data - this will now manually set sprung masses
    SetupWheelsSimulationData(WheelMass, WheelRadius, WheelWidth, WheelMOI,
        ChassisCMOffsetFVector, NumWheels, WheelsSimData, VerifyMass);

    // Verify sprung masses after setup
    for (PxU32 i = 0; i < static_cast<PxU32>(NumWheels); i++)
    {
        const PxVehicleSuspensionData& suspData = WheelsSimData->getSuspensionData(i);
        UE_LOG("[MyCarComponent] Wheel %d final sprung mass: %.2f", i, suspData.mSprungMass);

        if (suspData.mSprungMass <= 0.0f)
        {
            UE_LOG("[MyCarComponent] ERROR: Wheel %d has invalid sprung mass: %.2f", i, suspData.mSprungMass);
            VehicleActor->release();
            WheelsSimData->free();
            return;
        }
    }

    // Create drive simulation data
    PxVehicleDriveSimData4W DriveSimData;

    // Setup Ackermann geometry - Z-up 좌표계에 맞게 조정
    PxVehicleAckermannGeometryData AckermannData;
    AckermannData.mAccuracy = 1.0f;
    AckermannData.mFrontWidth = 2.0f; // 앞바퀴 간격 (X축 방향)
    AckermannData.mRearWidth = 2.0f;  // 뒷바퀴 간격 (X축 방향)
    AckermannData.mAxleSeparation = 3.0f; // 전후축 거리 (Y축 방향)
    DriveSimData.setAckermannGeometryData(AckermannData);

    // Setup differential (4WD) - PROPERLY INITIALIZE FIRST
    PxVehicleDifferential4WData Diff;
    // Initialize all required fields
    Diff.mType = PxVehicleDifferential4WData::eDIFF_TYPE_LS_4WD;
    Diff.mFrontRearSplit = 0.45f;        // 45% to front, 55% to rear
    Diff.mFrontLeftRightSplit = 0.5f;    // Equal split between front wheels
    Diff.mRearLeftRightSplit = 0.5f;     // Equal split between rear wheels
    Diff.mCentreBias = 1.3f;             // Center differential bias
    Diff.mFrontBias = 1.3f;              // Front differential bias
    Diff.mRearBias = 1.3f;               // Rear differential bias
    DriveSimData.setDiffData(Diff);

    // Setup engine with PROPER INITIALIZATION
    PxVehicleEngineData Engine;
    // Initialize all fields first
    Engine.mPeakTorque = 800.0f;
    Engine.mMaxOmega = 800.0f;
    Engine.mDampingRateFullThrottle = 0.15f;
    Engine.mDampingRateZeroThrottleClutchEngaged = 2.0f;
    Engine.mDampingRateZeroThrottleClutchDisengaged = 0.35f;

    // CRITICAL: Clear and setup torque curve properly
    Engine.mTorqueCurve.clear();
    Engine.mTorqueCurve.addPair(0.0f, 0.8f);   // Idle
    Engine.mTorqueCurve.addPair(0.33f, 1.0f);  // Peak torque at 33% RPM
    Engine.mTorqueCurve.addPair(0.5f, 0.95f);  // Mid-range
    Engine.mTorqueCurve.addPair(0.7f, 0.85f);  // High RPM falloff
    Engine.mTorqueCurve.addPair(1.0f, 0.7f);   // Max RPM

    DriveSimData.setEngineData(Engine);

    // Setup gears with PROPER INITIALIZATION
    PxVehicleGearsData Gears;
    Gears.mSwitchTime = 0.3f;
    Gears.mNbRatios = 6;
    Gears.mRatios[PxVehicleGearsData::eREVERSE] = -4.0f;
    Gears.mRatios[PxVehicleGearsData::eNEUTRAL] = 0.0f;
    Gears.mRatios[PxVehicleGearsData::eFIRST] = 4.0f;
    Gears.mRatios[PxVehicleGearsData::eSECOND] = 2.0f;
    Gears.mRatios[PxVehicleGearsData::eTHIRD] = 1.5f;
    Gears.mRatios[PxVehicleGearsData::eFOURTH] = 1.1f;
    Gears.mRatios[PxVehicleGearsData::eFIFTH] = 1.0f;
    Gears.mFinalRatio = 4.0f;
    DriveSimData.setGearsData(Gears);

    // Setup clutch with proper initialization
    PxVehicleClutchData Clutch;
    Clutch.mStrength = 10.0f;  // Default clutch strength
    DriveSimData.setClutchData(Clutch);

    // Setup auto-box with PROPER INITIALIZATION
    PxVehicleAutoBoxData AutoBox;
    // Initialize all gear ratios
    AutoBox.mUpRatios[PxVehicleGearsData::eFIRST] = 0.65f;
    AutoBox.mUpRatios[PxVehicleGearsData::eSECOND] = 0.65f;
    AutoBox.mUpRatios[PxVehicleGearsData::eTHIRD] = 0.65f;
    AutoBox.mUpRatios[PxVehicleGearsData::eFOURTH] = 0.65f;
    AutoBox.mUpRatios[PxVehicleGearsData::eFIFTH] = 0.65f;
    AutoBox.mDownRatios[PxVehicleGearsData::eREVERSE] = 0.5f;
    AutoBox.mDownRatios[PxVehicleGearsData::eFIRST] = 0.5f;
    AutoBox.mDownRatios[PxVehicleGearsData::eSECOND] = 0.5f;
    AutoBox.mDownRatios[PxVehicleGearsData::eTHIRD] = 0.5f;
    AutoBox.mDownRatios[PxVehicleGearsData::eFOURTH] = 0.5f;
    AutoBox.mDownRatios[PxVehicleGearsData::eFIFTH] = 0.5f;
    DriveSimData.setAutoBoxData(AutoBox);

    // Create the vehicle
    VehicleDrive4W = PxVehicleDrive4W::allocate(NumWheels);

    if (!VehicleDrive4W)
    {
        UE_LOG("[MyCarComponent] Failed to allocate vehicle");
        VehicleActor->release();
        WheelsSimData->free();
        return;
    }

    UE_LOG("[MyCarComponent] About to call vehicle setup...");

    // Setup - this is where the validation occurs
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
    
    // 2. Perform suspension raycasts using the new vehicle filter system
    PxVehicleWheels* vehicles[1] = {VehicleDrive4W};
    PxRaycastQueryResult* raycastResults = RaycastResults.GetData();
    const PxU32 raycastResultsSize = RaycastResults.Num();
    
    PxVehicleSuspensionRaycasts(
        BatchQuery,           // Batch query with vehicle pre-filter
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
    
	PhysScene->StepSimulation(1.0 / 60.0f);
    PhysScene->WaitForSimulation();

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
