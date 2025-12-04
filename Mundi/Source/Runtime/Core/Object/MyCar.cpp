#include "pch.h"
#include "MyCar.h"
#include "SkeletalMeshComponent.h"
#include "World.h"
#include "SceneComponent.h"
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
        PxVec3(-1.0f, 1.4f, -0.35f);  // 좌측 앞바퀴
    WheelCentreOffsets[PxVehicleDrive4WWheelOrder::eFRONT_RIGHT] =
        PxVec3(1.0f, 1.4f, -0.35f);   // 우측 앞바퀴

    // Rear wheels - 뒤쪽이 -Y 방향 (Z-up 왼손좌표계)
    WheelCentreOffsets[PxVehicleDrive4WWheelOrder::eREAR_LEFT] =
        PxVec3(-1.0f, -1.4f, -0.35f); // 좌측 뒷바퀴
    WheelCentreOffsets[PxVehicleDrive4WWheelOrder::eREAR_RIGHT] =
        PxVec3(1.0f, -1.4f, -0.35f);  // 우측 뒷바퀴

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

        // IMPROVED: Create tire data with better friction curve for traction
        PxVehicleTireData tireData;
        tireData.mType = 0;
        
        // 개선된 종방향 마찰 곡선 (longitudinal)
        tireData.mFrictionVsSlipGraph[0][0] = 0.0f;   // 슬립 0%
        tireData.mFrictionVsSlipGraph[0][1] = 1.0f;   // 마찰력 100%
        tireData.mFrictionVsSlipGraph[1][0] = 0.1f;   // 슬립 10%
        tireData.mFrictionVsSlipGraph[1][1] = 1.2f;   // 마찰력 120% (피크)
        tireData.mFrictionVsSlipGraph[2][0] = 1.0f;   // 슬립 100%
        tireData.mFrictionVsSlipGraph[2][1] = 0.9f;   // 마찰력 90%
        
        WheelsSimData->setTireData(i, tireData);

        // IMPROVED: Create suspension data with better spring settings
        PxVehicleSuspensionData suspData;
        suspData.mMaxCompression = 0.6f;    // 최대 압축 (더 큰 값)
        suspData.mMaxDroop = 0.4f;          // 최대 신장 (더 큰 값)
        suspData.mSpringStrength = 35000.0f; // 스프링 강성 증가
        suspData.mSpringDamperRate = 4500.0f; // 댐핑 증가

        // CRITICAL: Manually set the sprung mass
        suspData.mSprungMass = SprungMassPerWheel;

        UE_LOG("[SetupWheelsSimulationData] Wheel %d: Setting sprung mass to %.2f", i, suspData.mSprungMass);

        WheelsSimData->setSuspensionData(i, suspData);
        WheelsSimData->setWheelShapeMapping(i, i + 1);

        // Set wheel center offset
        WheelsSimData->setWheelCentreOffset(i, WheelCentreOffsets[i]);

        // Z-up 좌표계에서 서스펜션 이동 방향은 -Z (아래쪽)
        WheelsSimData->setSuspTravelDirection(i, PxVec3(0, 0, -1));

        // IMPROVED: Better suspension and tire force application points
        WheelsSimData->setSuspForceAppPointOffset(i, PxVec3(WheelCentreOffsets[i].x, WheelCentreOffsets[i].y, -0.2f));
        WheelsSimData->setTireForceAppPointOffset(i, PxVec3(WheelCentreOffsets[i].x, WheelCentreOffsets[i].y, -0.2f));

        // CRITICAL: Set scene query filter data to prevent vehicle from hitting itself
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
    RootComponent = CreateDefaultSubobject<USceneComponent>("RootComponent");

    VehicleMesh = CreateDefaultSubobject<USkeletalMeshComponent>("VehicleMesh");
    VehicleMesh->SetupAttachment(RootComponent, EAttachmentRule::KeepRelative);
    VehicleMesh->SetRelativeRotation(FQuat::MakeFromEulerZYX(FVector(0, 0, 90))); // Adjust orientation if needed
	VehicleMesh->SetRelativeLocation(FVector(0, 0, -0.8f));
    //SetRootComponent(VehicleMesh);
    VehicleMesh->SetSkeletalMesh("Data/Model/SkeletalCar.fbx");
    
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
    
    //VehicleMesh->SetSkeletalMesh("Data/Model/SkeletalCar.fbx");
	
    // Find wheel bones after mesh is available
    if(GWorld->bPie)
    {
        FindWheelBones();
    }
    
    
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
    bool bFound = false;
    for (UActorComponent* Component : OwnedComponents)
    {
        if (auto* Comp = Cast<USkeletalMeshComponent>(Component))
        {
            VehicleMesh = Comp;
            bFound = true;
            break;
        }
    }

    // If not found during duplication (e.g., loading from a level where it wasn't saved properly), recreate it.
    if (!bFound)
    {
        VehicleMesh = CreateDefaultSubobject<USkeletalMeshComponent>("VehicleMesh");
        VehicleMesh->SetupAttachment(RootComponent, EAttachmentRule::KeepRelative);
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
    
    // CRITICAL: Set proper basis vectors for Z-up coordinate system
    // Forward = +Y, Right = +X, Up = +Z in Z-up left-handed coordinate system
    PxVehicleSetBasisVectors(PxVec3(0, 0, 1), PxVec3(0, 1, 0));  // Up vector, Forward vector
    
    // Set update mode
    PxVehicleSetUpdateMode(PxVehicleUpdateMode::eVELOCITY_CHANGE);

    // 레이캐스트 결과 버퍼 초기화
    RaycastResults.SetNum(NumWheels);
    RaycastHitBuffer.SetNum(NumWheels);
    
    if (!SharedBatchQuery)
    {
        // Set up the raycast results buffer for this batch query
        PxBatchQueryDesc queryDesc(NumWheels, 0, 0);
        queryDesc.queryMemory.userRaycastResultBuffer = RaycastResults.GetData();
        queryDesc.queryMemory.userRaycastTouchBuffer = RaycastHitBuffer.GetData();
        queryDesc.queryMemory.raycastTouchBufferSize = NumWheels;

        SharedBatchQuery = FPhysXSharedResources::CreateVehicleBatchQuery(PxScenePtr, queryDesc);
        if (!SharedBatchQuery)
        {
            UE_LOG("[MyCarComponent] Failed to create shared BatchQuery");
            return;
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

    // Z-up 좌표계에서의 관性 모멘트 계산
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

    // IMPROVED: Setup engine with higher torque and better curve
    PxVehicleEngineData Engine;
    // Initialize all fields first
    Engine.mPeakTorque = 1200.0f;        // 증가된 피크 토크
    Engine.mMaxOmega = 600.0f;           // 최대 회전수
    Engine.mDampingRateFullThrottle = 0.15f;
    Engine.mDampingRateZeroThrottleClutchEngaged = 2.0f;
    Engine.mDampingRateZeroThrottleClutchDisengaged = 0.35f;

    // CRITICAL: Clear and setup torque curve properly with better low-end torque
    Engine.mTorqueCurve.clear();
    Engine.mTorqueCurve.addPair(0.0f, 0.9f);   // 아이들에서 높은 토크
    Engine.mTorqueCurve.addPair(0.2f, 1.0f);   // 피크 토크를 낮은 RPM에서
    Engine.mTorqueCurve.addPair(0.4f, 0.98f);  // 중간 범위 유지
    Engine.mTorqueCurve.addPair(0.6f, 0.9f);   // 고RPM에서 감소
    Engine.mTorqueCurve.addPair(0.8f, 0.8f);   // 더 큰 감소
    Engine.mTorqueCurve.addPair(1.0f, 0.7f);   // 최대 RPM

    DriveSimData.setEngineData(Engine);

    // IMPROVED: Setup gears with better ratios for acceleration
    PxVehicleGearsData Gears;
    Gears.mSwitchTime = 0.5f;
    Gears.mNbRatios = 6;
    Gears.mRatios[PxVehicleGearsData::eREVERSE] = -4.5f; // 후진 기어비 증가
    Gears.mRatios[PxVehicleGearsData::eNEUTRAL] = 0.0f;
    Gears.mRatios[PxVehicleGearsData::eFIRST] = 5.0f;    // 1단 기어비 증가 (가속력 향상)
    Gears.mRatios[PxVehicleGearsData::eSECOND] = 3.0f;   // 2단 기어비 증가
    Gears.mRatios[PxVehicleGearsData::eTHIRD] = 2.0f;    // 3단 기어비 증가
    Gears.mRatios[PxVehicleGearsData::eFOURTH] = 1.5f;
    Gears.mRatios[PxVehicleGearsData::eFIFTH] = 1.0f;
    Gears.mFinalRatio = 4.5f;                            // 파이널 기어비 증가
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

    // IMPROVED: Better initialization
    VehicleDrive4W->setToRestState();
    VehicleDrive4W->mDriveDynData.forceGearChange(PxVehicleGearsData::eFIRST);
    VehicleDrive4W->mDriveDynData.setUseAutoGears(true);

    // 초기 엔진 회전수를 높게 설정하여 즉시 토크 생성
    VehicleDrive4W->mDriveDynData.setEngineRotationSpeed(200.0f);
    
    // CRITICAL: Wake up the vehicle to ensure it's not sleeping
    VehicleActor->wakeUp();
    
    // Free simulation data
    WheelsSimData->free();

    UE_LOG("[MyCarComponent] Vehicle 4W created successfully with improved settings");
}

void AMyCar::ProcessKeyboardInput(float DeltaTime)
{
    UInputManager& InputManager = UInputManager::GetInstance();
    
    // Reset inputs
    float TargetThrottle = 0.0f;
    float TargetBrake = 0.0f;
    float TargetSteer = 0.0f;
    float TargetHandbrake = 0.0f;
    bool bTargetReverse = false;
    
    // 현재 차량 속도 확인 (전진/후진 판단용)
    float CurrentSpeed = 0.0f;
    if (VehicleDrive4W)
    {
        CurrentSpeed = VehicleDrive4W->computeForwardSpeed();
    }
    
    // Arrow key controls
    if (InputManager.IsKeyDown(VK_UP))
    {
        // 위 화살표: 전진 또는 후진에서 브레이크
        if (CurrentSpeed < -0.1f) // 후진 중이면 브레이크
        {
            TargetBrake = 1.0f;
        }
        else // 정지 또는 전진 중이면 가속
        {
            TargetThrottle = 1.0f;
            bTargetReverse = false;
        }
    }
    if (InputManager.IsKeyDown(VK_DOWN))
    {
        // 아래 화살표: 후진 또는 전진에서 브레이크
        if (CurrentSpeed > 0.1f) // 전진 중이면 브레이크
        {
            TargetBrake = 1.0f;
        }
        else // 정지 또는 후진 중이면 후진 가속
        {
            TargetThrottle = 1.0f;
            bTargetReverse = true;
        }
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
    bIsReversing = bTargetReverse;
    
    // Apply to vehicle
    VehicleInput.AnalogAccel = CurrentThrottleInput;
    VehicleInput.AnalogBrake = CurrentBrakeInput;
    VehicleInput.AnalogSteer = CurrentSteerInput;
    VehicleInput.AnalogHandbrake = CurrentHandbrakeInput;
    VehicleInput.bReverse = bIsReversing;
}

void AMyCar::UpdateVehiclePhysics(float DeltaTime)
{
    if (!VehicleDrive4W)
        return;

    // CRITICAL: Check if vehicle is sleeping and wake it up if input is provided
    PxRigidDynamic* VehicleActor = VehicleDrive4W->getRigidDynamicActor();
    if (VehicleActor && VehicleActor->isSleeping())
    {
        // Wake up vehicle if there's any input
        if (VehicleInput.AnalogAccel > 0.01f || 
            VehicleInput.AnalogBrake > 0.01f || 
            FMath::Abs(VehicleInput.AnalogSteer) > 0.01f ||
            VehicleInput.AnalogHandbrake > 0.01f)
        {
            VehicleActor->wakeUp();
            //UE_LOG("[MyCarComponent] Vehicle woken up due to input");
        }
    }

    // Update actor transform first
    PxTransform PxTrans = VehicleActor->getGlobalPose();
    FTransform NewTransform;
    NewTransform.Translation = FVector(PxTrans.p.x, PxTrans.p.y, PxTrans.p.z);
    NewTransform.Rotation = FQuat(PxTrans.q.x, PxTrans.q.y, PxTrans.q.z, PxTrans.q.w);
    NewTransform.Scale3D = FVector(1, 1, 1);
    SetActorTransform(NewTransform);

    UWorld* World = GetWorld();
    if (!World || !World->GetPhysScene())
        return;
    
    FPhysScene* PhysScene = World->GetPhysScene();
    PxScene* PxScenePtr = PhysScene->GetScene();
    
    if (!PxScenePtr)
        return;
    
    // 후진 기어 처리
    if (VehicleInput.bReverse)
    {
        // 후진 기어로 변경
        VehicleDrive4W->mDriveDynData.forceGearChange(PxVehicleGearsData::eREVERSE);
        VehicleDrive4W->mDriveDynData.setUseAutoGears(false); // 수동 기어 모드
    }
    else
    {
        // 전진 기어 모드
        PxU32 currentGear = VehicleDrive4W->mDriveDynData.getCurrentGear();
        if (currentGear == PxVehicleGearsData::eREVERSE)
        {
            // 후진에서 전진으로 변경할 때
            VehicleDrive4W->mDriveDynData.forceGearChange(PxVehicleGearsData::eFIRST);
        }
        VehicleDrive4W->mDriveDynData.setUseAutoGears(true); // 자동 기어 모드
    }
    
    // 1. Apply input with better responsiveness
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
    
    // 2. Perform suspension raycasts using the shared vehicle resources
    PxVehicleWheels* vehicles[1] = {VehicleDrive4W};
    PxRaycastQueryResult* raycastResults = RaycastResults.GetData();
    const PxU32 raycastResultsSize = RaycastResults.Num();
    
    // Get shared vehicle friction pairs for raycasts
    PxVehicleDrivableSurfaceToTireFrictionPairs* SharedFrictionPairs = FPhysXSharedResources::GetVehicleFrictionPairs();
    if (!SharedFrictionPairs)
    {
        UE_LOG("[MyCarComponent] SharedFrictionPairs is null - vehicle suspension may not work properly");
        return;
    }

    // Use shared BatchQuery - create one if it doesn't exist yet
    

    PxVehicleSuspensionRaycasts(
        SharedBatchQuery,     // Shared batch query with vehicle pre-filter
        1,                    // Number of vehicles
        vehicles,             // Array of vehicle pointers
        raycastResultsSize,   // Size of raycast results array
        raycastResults        // Output raycast results
    );

    // 3. Prepare wheel query results from raycast hits
    PxWheelQueryResult wheelQueryResults[PX_MAX_NB_WHEELS];
    const PxU32 numWheels = VehicleDrive4W->mWheelsSimData.getNbWheels();

    // ENHANCED: 레이캐스트 결과 디버깅과 문제 진단
    int32 wheelContactCount = 0;
    for (PxU32 i = 0; i < numWheels; i++)
    {
        if (raycastResults[i].getNbAnyHits() > 0)
        {
            wheelContactCount++;
            const PxRaycastHit& hit = raycastResults[i].getAnyHit(0);
            /*UE_LOG("[MyCarComponent] Wheel %d: Hit distance = %.3f, Normal = (%.2f, %.2f, %.2f)",
                i, hit.distance, hit.normal.x, hit.normal.y, hit.normal.z);*/
        }
        else
        {
            //UE_LOG("[MyCarComponent] Wheel %d: NO CONTACT - wheel may be in air", i);
        }
    }
    
    //UE_LOG("[MyCarComponent] Wheels in contact: %d/4", wheelContactCount);
    
    PxVehicleWheelQueryResult vehicleQueryResults[1] = {
        {wheelQueryResults, numWheels}
    };
    
    // 4. Update vehicle physics using shared friction pairs
    const PxVec3 grav = PxScenePtr->getGravity();
    //const float FixedTimeStep = 1.0f / 60.0f;
    float FixedTimeStep = 0.0f;
    if(GWorld->bPie)
    {
        FixedTimeStep = GWorld->GetDeltaTime(EDeltaTime::Game);
    }
    else
    {
        FixedTimeStep = DeltaTime;
	}
    
    PxVehicleUpdates(FixedTimeStep * 2.0f, grav, *SharedFrictionPairs, 1, vehicles, vehicleQueryResults);

    // ENHANCED: Detailed vehicle state debugging
    bool bIsSleeping = VehicleActor->isSleeping();
    bIsVehicleInAir = PxVehicleIsInAir(vehicleQueryResults[0]);
    
    float forwardSpeed = VehicleDrive4W->computeForwardSpeed();
    float engineRotationSpeed = VehicleDrive4W->mDriveDynData.getEngineRotationSpeed();
    PxU32 currentGear = VehicleDrive4W->mDriveDynData.getCurrentGear();
    
    // Check tire forces and suspension forces
    for (PxU32 i = 0; i < numWheels; i++)
    {
        // PhysX Vehicle SDK에서 지원하는 함수들로 교체
        const PxVehicleSuspensionData& suspData = VehicleDrive4W->mWheelsSimData.getSuspensionData(i);
        float sprungMass = suspData.mSprungMass;
        
        //UE_LOG("[MyCarComponent] Wheel %d: SprungMass=%.1f", i, sprungMass);
    }

    // 5. Enhanced logging
    //static float LogCounter = 0.0f;
    //if (LogCounter >= 1.0f) // Log every second at 60 FPS
    //{
    //    LogCounter = 0.0f;
    //    UE_LOG("[MyCarComponent] === Vehicle State ===");
    //    UE_LOG("[MyCarComponent] Sleeping: %s | InAir: %s | Gear: %d | Reverse: %s",
    //        bIsSleeping ? "YES" : "NO", 
    //        bIsVehicleInAir ? "YES" : "NO", 
    //        currentGear,
    //        bIsReversing ? "YES" : "NO");
    //    UE_LOG("[MyCarComponent] Speed: %.2f m/s | Engine RPM: %.1f",
    //        forwardSpeed, engineRotationSpeed);
    //    UE_LOG("[MyCarComponent] Input - Accel: %.2f | Brake: %.2f | Steer: %.2f",
    //        VehicleInput.AnalogAccel, VehicleInput.AnalogBrake, VehicleInput.AnalogSteer);
    //    UE_LOG("[MyCarComponent] Position: (%.2f, %.2f, %.2f)",
    //        PxTrans.p.x, PxTrans.p.y, PxTrans.p.z);
    //    UE_LOG("[MyCarComponent] =====================");
    //}
    //LogCounter += DeltaTime;
    
    // Update wheel bone rotations based on PhysX wheel rotation
    UpdateWheelBoneRotations(vehicleQueryResults[0]);
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
    
    if (SharedBatchQuery)
    {
        //FPhysScene* PhysScene = World->GetPhysScene();
        FPhysXSharedResources::ReleaseVehicleBatchQuery(SharedBatchQuery);
        SharedBatchQuery = nullptr;
    }

    /*if (BatchQuery)
    {
        BatchQuery->release();
        BatchQuery = nullptr;
    }
    
    if (FrictionPairs)
    {
        FrictionPairs->release();
        FrictionPairs = nullptr;
    }*/
    
    // Cleanup PhysX Vehicle SDK
    //PxCloseVehicleSDK();
    
    //UE_LOG("[MyCarComponent] Vehicle physics cleaned up");
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

void AMyCar::ApplyReverse(bool bInReverse)
{
    VehicleInput.bReverse = bInReverse;
    bIsReversing = bInReverse;
}

void AMyCar::FindWheelBones()
{
    if (!VehicleMesh)
    {
        UE_LOG("[MyCarComponent] FindWheelBones: No VehicleMesh found");
        return;
    }

    // Find wheel bones by name
    // PhysX wheel order: FL(0), FR(1), RL(2), RR(3)
    WheelBoneIndices[0] = VehicleMesh->GetBoneIndexByName(FName("FL")); // Front Left
    WheelBoneIndices[1] = VehicleMesh->GetBoneIndexByName(FName("FR")); // Front Right
    WheelBoneIndices[2] = VehicleMesh->GetBoneIndexByName(FName("RL")); // Rear Left (changed from RR to RL)
    WheelBoneIndices[3] = VehicleMesh->GetBoneIndexByName(FName("RR")); // Rear Right

    // Check if all wheel bones were found
    bWheelBonesFound = true;
    for (int32 i = 0; i < 4; i++)
    {
        if (WheelBoneIndices[i] == INDEX_NONE)
        {
            bWheelBonesFound = false;
            //UE_LOG("[MyCarComponent] Wheel bone %d not found", i);
        }
        else
        {
            //UE_LOG("[MyCarComponent] Found wheel bone %d at index %d", i, WheelBoneIndices[i]);
        }
    }

    /*if (bWheelBonesFound)
    {
        UE_LOG("[MyCarComponent] All wheel bones found successfully");
    }
    else
    {
        UE_LOG("[MyCarComponent] WARNING: Some wheel bones not found. Wheel rotation animation will be disabled.");
    }*/
}

void AMyCar::UpdateWheelBoneRotations(const PxVehicleWheelQueryResult& Result)
{
    if (!bWheelBonesFound || !VehicleDrive4W || !VehicleMesh)
    {
        return;
    }

    // Track cumulative wheel spin rotation for each wheel
    static float CumulativeWheelSpin[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    // Get wheel rotation speeds from PhysX vehicle
    for (int32 WheelIdx = 0; WheelIdx < 4; WheelIdx++)
    {
        if (WheelBoneIndices[WheelIdx] == INDEX_NONE)
            continue;

        // Get wheel rotation speed (rad/s) from PhysX
        const PxF32 WheelRotationSpeed = VehicleDrive4W->mWheelsDynData.getWheelRotationSpeed(WheelIdx);

        // Calculate rotation delta based on wheel speed
        UWorld* World = GetWorld();
        float DeltaTime = World ? World->GetDeltaTime(EDeltaTime::Game) : 0.016f;

        float RotationDelta = 0.0f;
        if (WheelIdx % 2 == 0)
        {
            RotationDelta = -WheelRotationSpeed * DeltaTime;
        }
        else
        {
            RotationDelta = WheelRotationSpeed * DeltaTime;
        }

        // Accumulate wheel spin rotation
        CumulativeWheelSpin[WheelIdx] += RotationDelta;

        // Get current bone transform
        FTransform CurrentBoneTransform = VehicleMesh->GetBoneLocalTransform(WheelBoneIndices[WheelIdx]);

        // For front wheels (FL: 0, FR: 1), apply both steering and cumulative spin
        if (WheelIdx == 0 || WheelIdx == 1) // Front Left and Front Right
        {
            // Get steer angle from PhysX (in radians)
            float SteerAngle = Result.wheelQueryResults[WheelIdx].steerAngle;
            float SteerAngleDegree = RadiansToDegrees(SteerAngle);

            // Apply both rotations using cumulative spin
            float XAngle = WheelIdx == 0 ? 0.0f : 180.0f;
            float YAngle = -RadiansToDegrees(CumulativeWheelSpin[WheelIdx]); // Cumulative spin in degrees
            float ZAngle = -SteerAngleDegree + 90.0f; // Steering

            FVector NewEuler = FVector(XAngle, YAngle, ZAngle);
            CurrentBoneTransform.Rotation = FQuat::MakeFromEulerZYX(NewEuler);

            /*UE_LOG("[MyCarComponent] Wheel %d - Cumulative spin: %.2f deg, Steer: %.2f deg",
                WheelIdx, YAngle, SteerAngleDegree);*/
        }
        else
        {
            // For rear wheels, just apply cumulative spin rotation
            float YAngle = -RadiansToDegrees(CumulativeWheelSpin[WheelIdx]);
            //float XAngle = WheelIdx == 2 ? 0.0f : 180.0f; // Rear wheels orientation

			FVector RotationEuler = CurrentBoneTransform.Rotation.ToEulerZYXDeg();

            FVector NewEuler = FVector(RotationEuler.X, YAngle, RotationEuler.Z);
            CurrentBoneTransform.Rotation = FQuat::MakeFromEulerZYX(NewEuler);
        }

        CurrentBoneTransform.Rotation.Normalize();

        // Set the updated bone transform
        VehicleMesh->SetBoneLocalTransform(WheelBoneIndices[WheelIdx], CurrentBoneTransform);

        // Optional: Log wheel rotation for debugging (every 60 frames)
        static int32 LogFrameCounter = 0;
        /*if (LogFrameCounter++ % 60 == 0 && (WheelIdx == 0 || WheelIdx == 1))
        {
            UE_LOG("[MyCarComponent] Wheel %d - Spin speed: %.2f rad/s, Cumulative: %.2f deg",
                WheelIdx, WheelRotationSpeed, RadiansToDegrees(CumulativeWheelSpin[WheelIdx]));
        }*/
    }
}
