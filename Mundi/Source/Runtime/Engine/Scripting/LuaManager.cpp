#include "pch.h"
#include "LuaManager.h"
#include "LuaComponentProxy.h"
#include "GameObject.h"
#include "ObjectIterator.h"
#include "CameraActor.h"
#include "CameraComponent.h"
#include "PlayerCameraManager.h"
#include <tuple>

sol::object MakeCompProxy(sol::state_view SolState, void* Instance, UClass* Class) {
    BuildBoundClass(Class);
    LuaComponentProxy Proxy;
    Proxy.Instance = Instance;
    Proxy.Class = Class;
    return sol::make_object(SolState, std::move(Proxy));
}

FLuaManager::FLuaManager()
{
    Lua = new sol::state();
    
    
    // Open essential standard libraries for gameplay scripts
    Lua->open_libraries(
        sol::lib::base,
        sol::lib::coroutine,
        sol::lib::math,
        sol::lib::table,
        sol::lib::string
    );

    SharedLib = Lua->create_table();

    // Lua에서 Actor와 FGameObject 가 1대1로 매칭되고
    // Component는 그대로 Component와 1대1로 매칭된다
    // NOTE: 그냥 FGameObject 개념을 없애고 그냥 Actor/Component 그대로 사용하는 게 좋을듯?
    Lua->new_usertype<FGameObject>("GameObject",
        "UUID", &FGameObject::UUID,
        "Tag", sol::property(&FGameObject::GetTag, &FGameObject::SetTag),
        "Location", sol::property(&FGameObject::GetLocation, &FGameObject::SetLocation),
        "Rotation", sol::property(&FGameObject::GetRotation, &FGameObject::SetRotation), 
        "Scale", sol::property(&FGameObject::GetScale, &FGameObject::SetScale),
        "bIsActive", sol::property(&FGameObject::GetIsActive, &FGameObject::SetIsActive),
        "Velocity", &FGameObject::Velocity,
        "PrintLocation", &FGameObject::PrintLocation,
        "GetForward", &FGameObject::GetForward
    );
    
    Lua->new_usertype<UCameraComponent>("CameraComponent",
        sol::no_constructor,
        "SetLocation", sol::overload(
            [](UCameraComponent* Camera, FVector Location)
            {
                if (!Camera)
                {
                    return;
                }
                Camera->SetWorldLocation(Location);
            },
            [](UCameraComponent* Camera, float X, float Y, float Z)
            {
                if (!Camera)
                {
                    return;
                }
                Camera->SetWorldLocation(FVector(X, Y, Z));
            }
        ),
        "SetCameraForward",
        [](UCameraComponent* Camera, FVector Direction)
        {
            if (!Camera)
            {
                return;
            }
            ACameraActor* CameraActor = Cast<ACameraActor>(Camera->GetOwner());
            CameraActor->SetForward(Direction);
        },
        "GetActorLocation", [](UCameraComponent* Camera) -> FVector
        {
            if (!Camera)
            {
                // 유효하지 않은 경우 0 벡터 반환
                return FVector(0.f, 0.f, 0.f);
            }
            return Camera->GetWorldLocation();
        },
        "GetActorRight", [](UCameraComponent* Camera) -> FVector
        {
            if (!Camera) return FVector(0.f, 0.f, 1.f); // 기본값 (World Forward)

            // C++ UCameraComponent 클래스의 실제 함수명으로 변경해야 합니다.
            // (예: GetActorForwardVector(), GetForward() 등)
            return Camera->GetForward();
        }
    );
    Lua->new_usertype<UInputManager>("InputManager",
        "IsKeyDown", sol::overload(
            &UInputManager::IsKeyDown,
            [](UInputManager* Self, const FString& Key) {
                if (Key.empty()) return false;
                return Self->IsKeyDown(Key[0]);
            }),
        "IsKeyPressed", sol::overload(
            &UInputManager::IsKeyPressed,
            [](UInputManager* Self, const FString& Key) {
                if (Key.empty()) return false;
                return Self->IsKeyPressed(Key[0]);
            }),
        "IsKeyReleased", sol::overload(
            &UInputManager::IsKeyReleased,
            [](UInputManager* Self, const FString& Key) {
                if (Key.empty()) return false;
                return Self->IsKeyReleased(Key[0]);
            }),
        "IsMouseButtonDown", &UInputManager::IsMouseButtonDown,
        "IsMouseButtonPressed", &UInputManager::IsMouseButtonPressed,
        "IsMouseButtonReleased", &UInputManager::IsMouseButtonReleased,
        "GetMouseDelta", [](UInputManager* Self) {
            const FVector2D Delta = Self->GetMouseDelta();
            return FVector(Delta.X, Delta.Y, 1.0);
        },
        "SetCursorVisible", [](UInputManager* Self, bool bVisible){
            if (bVisible)
            { 
                Self->SetCursorVisible(true);
                if (Self->IsCursorLocked())
                    Self->ReleaseCursor();
            } else
            { 
                Self->SetCursorVisible(false);
                Self->LockCursor();
            }
        }
    );                
    
    sol::table MouseButton = Lua->create_table("MouseButton");
    MouseButton["Left"] = EMouseButton::LeftButton;
    MouseButton["Right"] = EMouseButton::RightButton;
    MouseButton["Middle"] = EMouseButton::MiddleButton;
    MouseButton["XButton1"] = EMouseButton::XButton1;
    MouseButton["XButton2"] = EMouseButton::XButton2;
    
    Lua->set_function("print", sol::overload(                             
        [](const FString& msg) {                                          
            UE_LOG("[Lua-Str] %s\n", msg.c_str());                        
        },                                                                
                                                                          
        [](int num){                                                      
            UE_LOG("[Lua] %d\n", num);                                    
        },                                                                
                                                                          
        [](double num){                                                   
            UE_LOG("[Lua] %f\n", num);                                    
        },                                                                
                                                                          
        [](FVector Vector)                                                    
        {                                                                 
            UE_LOG("[Lua] (%f, %f, %f)\n", Vector.X, Vector.Y, Vector.Z); 
        }                                                                 
    ));
    
    // GlobalConfig는 전역 table
    SharedLib["GlobalConfig"] = Lua->create_table(); 
    // SharedLib["GlobalConfig"]["Gravity"] = 9.8;

    SharedLib.set_function("SpawnPrefab", sol::overload(
        [](const FString& PrefabPath) -> FGameObject*
        {
            FGameObject* NewObject = nullptr;

            AActor* NewActor = GWorld->SpawnPrefabActor(UTF8ToWide(PrefabPath));

            if (NewActor)
            {
                NewObject = NewActor->GetGameObject();
            }

            return NewObject;
        }
    ));
    SharedLib.set_function("DeleteObject", sol::overload(
        [](const FGameObject& GameObject)
        {
            for (TObjectIterator<AActor> It; It; ++It)
            {
                AActor* Actor = *It;

                if (Actor->UUID == GameObject.UUID)
                {
                    Actor->Destroy();   // 지연 삭제 요청 (즉시 삭제하면 터짐)
                    break;
                }
            }
        }
    ));
    SharedLib.set_function("FindObjectByName",
        [](const FString& ActorName) -> FGameObject*
        {
            if (!GWorld)
            {
                return nullptr;
            }

            // Lua의 FString을 FName으로 변환
            FName NameToFind(ActorName);

            AActor* FoundActor = GWorld->FindActorByName(NameToFind);

            // Lua 스크립트가 사용할 수 있도록 AActor*를 FGameObject*로 변환
            if (FoundActor && !FoundActor->IsPendingDestroy())
            {
                return FoundActor->GetGameObject();
            }

            return nullptr; // 찾지 못함
        }
    );
    SharedLib.set_function("FindComponentByName",
        [this](const FString& ComponentName) -> UActorComponent*
        {
            if (!GWorld)
            {
                return nullptr;
            }

            // Lua의 FString을 FName으로 변환
            FName NameToFind(ComponentName);

            UActorComponent* FoundComponent = GWorld->FindComponentByName(NameToFind);

            // Lua 스크립트가 사용할 수 있도록 UActorComponent*를 LuaComponentProxy로 변환
            if (FoundComponent && !FoundComponent->IsPendingDestroy())
            {
                return FoundComponent;
            }

            return nullptr; // 찾지 못함
        }
    );
    SharedLib.set_function("GetCamera",
        []() -> UCameraComponent*
        {
            if (!GWorld)
            {
                return nullptr;
            }
            return GWorld->GetPlayerCameraManager()->GetViewCamera();
        }
    );
    SharedLib.set_function("GetCameraManager",
        []() -> APlayerCameraManager*
        {
            if (!GWorld)
            {
                return nullptr;
            }
            return GWorld->GetPlayerCameraManager();
        }
    );
    SharedLib.set_function("SetPlayerForward",
        [](FGameObject& GameObject, FVector Direction)
        {
            AActor* Player = GameObject.GetOwner();
            if (!Player)
            {
                return;
            }

            USceneComponent* SceneComponent = Player->GetRootComponent();

            if (!SceneComponent)
            {
                return;
            }

            SceneComponent->SetForward(Direction);
        }
   );
    SharedLib.set_function("Vector", sol::overload(
       []() { return FVector(0.0f, 0.0f, 0.0f); },
       [](float x, float y, float z) { return FVector(x, y, z); }
   ));

    //@TODO(Timing)
    SharedLib.set_function("SetSlomo", [](float Duration , float Dilation) { GWorld->RequestSlomo(Duration, Dilation); });

    SharedLib.set_function("HitStop", [](float Duration, sol::optional<float> Scale) { GWorld->RequestHitStop(Duration, Scale.value_or(0.0f)); });
    
    SharedLib.set_function("TargetHitStop", [](FGameObject& Obj, float Duration, sol::optional<float> Scale) 
        {
            if (AActor* Owner = Obj.GetOwner())
            {
                Owner->SetCustomTimeDillation(Duration, Scale.value_or(0.0f));
            }
        });
    
    // FVector usertype 등록 (메서드와 프로퍼티)
    SharedLib.new_usertype<FVector>("FVector",
        sol::no_constructor,  // 생성자는 위에서 Vector 함수로 등록했음
        // Properties
        "X", &FVector::X,
        "Y", &FVector::Y,
        "Z", &FVector::Z,
        // Operators
        sol::meta_function::addition, [](const FVector& a, const FVector& b) -> FVector {
            return FVector(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
        },
        sol::meta_function::subtraction, [](const FVector& a, const FVector& b) -> FVector {
            return FVector(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
        },
        sol::meta_function::multiplication, sol::overload(
            [](const FVector& v, float f) -> FVector { return v * f; },
            [](float f, const FVector& v) -> FVector { return v * f; }
        ),
        // Methods
        "Length", &FVector::Distance,
        "Normalize", &FVector::Normalize,
        "Dot", [](const FVector& a, const FVector& b) { return FVector::Dot(a, b); },
        "Cross", [](const FVector& a, const FVector& b) { return FVector::Cross(a, b); }
    );

    Lua->set_function("Color", sol::overload(
        []() { return FLinearColor(0.0f, 0.0f, 0.0f, 1.0f); },
        [](float R, float G, float B) { return FLinearColor(R, G, B, 1.0f); },
        [](float R, float G, float B, float A) { return FLinearColor(R, G, B, A); }
    ));


    SharedLib.new_usertype<FLinearColor>("FLinearColor",
        sol::no_constructor,
        "R", &FLinearColor::R,
        "G", &FLinearColor::G,
        "B", &FLinearColor::B,
        "A", &FLinearColor::A
    );

    RegisterComponentProxy(*Lua);
    ExposeGlobalFunctions();
    ExposeAllComponentsToLua();

    // 위 등록 마친 뒤 fall back 설정 : Shared lib의 fall back은 G
    sol::table MetaTableShared = Lua->create_table();
    MetaTableShared[sol::meta_function::index] = Lua->globals();
    SharedLib[sol::metatable_key]  = MetaTableShared;
}

FLuaManager::~FLuaManager()
{
    ShutdownBeforeLuaClose();
    
    if (Lua)
    {
        delete Lua;
        Lua = nullptr;
    }
}

sol::environment FLuaManager::CreateEnvironment()
{
    sol::environment Env(*Lua, sol::create);

    // Environment의 Fall back은 SharedLib
    sol::table MetaTable = Lua->create_table();
    MetaTable[sol::meta_function::index] = SharedLib;
    Env[sol::metatable_key] = MetaTable;
    
    return Env;
}

void FLuaManager::RegisterComponentProxy(sol::state& Lua) {
    Lua.new_usertype<LuaComponentProxy>("Component",
        sol::meta_function::index,     &LuaComponentProxy::Index,
        sol::meta_function::new_index, &LuaComponentProxy::NewIndex
    );
}

void FLuaManager::ExposeAllComponentsToLua()
{
    SharedLib["Components"] = Lua->create_table();

    SharedLib.set_function("AddComponent",
        [this](sol::object Obj, const FString& ClassName)
        {
           if (!Obj.is<FGameObject&>()) {
                UE_LOG("[Lua][error] Error: Expected GameObject\n");
                return sol::make_object(*Lua, sol::nil);
            }
        
            FGameObject& GameObject = Obj.as<FGameObject&>();
            AActor* Actor = GameObject.GetOwner();

            UClass* Class = UClass::FindClass(ClassName);
            if (!Class) return sol::make_object(*Lua, sol::nil);

            UActorComponent* Comp = Actor->AddNewComponent(Class);
            return MakeCompProxy(*Lua, Comp, Class);
        });

    SharedLib.set_function("GetComponent",
        [this](sol::object Obj, const FString& ClassName)
        {
            if (!Obj.is<FGameObject&>()) {
                UE_LOG("[Lua][error] Error: Expected GameObject\n");
                return sol::make_object(*Lua, sol::nil);
            }
            
            FGameObject& GameObject = Obj.as<FGameObject&>();
            AActor* Actor = GameObject.GetOwner();

            UClass* Class = UClass::FindClass(ClassName);
            if (!Class) return sol::make_object(*Lua, sol::nil);
            
            UActorComponent* Comp = Actor->GetComponent(Class);
            if (!Comp) return sol::make_object(*Lua, sol::nil); 
            
            return MakeCompProxy(*Lua, Comp, Class);
        }
    );
}

void FLuaManager::ExposeGlobalFunctions()
{
    // APlayerCameraManager 클래스의 멤버 함수들 바인딩
    Lua->new_usertype<APlayerCameraManager>("PlayerCameraManager",
        sol::no_constructor,

        // --- StartCameraShake ---
        "StartCameraShake", sol::overload(
            // (Full) 5개 인수
            [](APlayerCameraManager* Self, float InDuration, float AmpLoc, float AmpRotDeg, float Frequency, int32 InPriority)
            {
                if (Self) Self->StartCameraShake(InDuration, AmpLoc, AmpRotDeg, Frequency, InPriority);
            },
            // (Priority 기본값 사용) 4개 인수
            [](APlayerCameraManager* Self, float InDuration, float AmpLoc, float AmpRotDeg, float Frequency)
            {
                if (Self) Self->StartCameraShake(InDuration, AmpLoc, AmpRotDeg, Frequency);
            }
        ),

        // --- StartFade ---
        "StartFade", sol::overload(
            // (Full) 5개 인수
            [](APlayerCameraManager* Self, float InDuration, float FromAlpha, float ToAlpha, const FLinearColor& InColor, int32 InPriority)
            {
                if (Self) Self->StartFade(InDuration, FromAlpha, ToAlpha, InColor, InPriority);
            },
            // (Priority 기본값 사용) 4개 인수
            [](APlayerCameraManager* Self, float InDuration, float FromAlpha, float ToAlpha, const FLinearColor& InColor)
            {
                if (Self) Self->StartFade(InDuration, FromAlpha, ToAlpha, InColor);
            },
            // (Color, Priority 기본값 사용) 3개 인수
            [](APlayerCameraManager* Self, float InDuration, float FromAlpha, float ToAlpha)
            {
                if (Self) Self->StartFade(InDuration, FromAlpha, ToAlpha);
            }
        ),

        // --- StartLetterBox ---
        "StartLetterBox", sol::overload(
            // (Full) 5개 인수
            [](APlayerCameraManager* Self, float InDuration, float Aspect, float BarHeight, const FLinearColor& InColor, int32 InPriority)
            {
                if (Self) Self->StartLetterBox(InDuration, Aspect, BarHeight, InColor, InPriority);
            },
            // (Priority 기본값 사용) 4개 인수
            [](APlayerCameraManager* Self, float InDuration, float Aspect, float BarHeight, const FLinearColor& InColor)
            {
                if (Self) Self->StartLetterBox(InDuration, Aspect, BarHeight, InColor);
            },
            // (Color, Priority 기본값 사용) 3개 인수
            [](APlayerCameraManager* Self, float InDuration, float Aspect, float BarHeight)
            {
                if (Self) Self->StartLetterBox(InDuration, Aspect, BarHeight);
            }
        ),

        // --- StartVignette (int 반환) ---
        "StartVignette", sol::overload(
            // (Full) 7개 인수
            [](APlayerCameraManager* Self, float InDuration, float Radius, float Softness, float Intensity, float Roundness, const FLinearColor& InColor, int32 InPriority) -> int
            {
                return Self ? Self->StartVignette(InDuration, Radius, Softness, Intensity, Roundness, InColor, InPriority) : -1;
            },
            // (Priority 기본값 사용) 6개 인수
            [](APlayerCameraManager* Self, float InDuration, float Radius, float Softness, float Intensity, float Roundness, const FLinearColor& InColor) -> int
            {
                return Self ? Self->StartVignette(InDuration, Radius, Softness, Intensity, Roundness, InColor) : -1;
            },
            // (Color, Priority 기본값 사용) 5개 인수
            [](APlayerCameraManager* Self, float InDuration, float Radius, float Softness, float Intensity, float Roundness) -> int
            {
                return Self ? Self->StartVignette(InDuration, Radius, Softness, Intensity, Roundness) : -1;
            }
        ),

        // --- UpdateVignette (int 반환) ---
        "UpdateVignette", sol::overload(
            // (Full) 8개 인수
            [](APlayerCameraManager* Self, int Idx, float InDuration, float Radius, float Softness, float Intensity, float Roundness, const FLinearColor& InColor, int32 InPriority) -> int
            {
                return Self ? Self->UpdateVignette(Idx, InDuration, Radius, Softness, Intensity, Roundness, InColor, InPriority) : -1;
            },
            // (Priority 기본값 사용) 7개 인수
            [](APlayerCameraManager* Self, int Idx, float InDuration, float Radius, float Softness, float Intensity, float Roundness, const FLinearColor& InColor) -> int
            {
                return Self ? Self->UpdateVignette(Idx, InDuration, Radius, Softness, Intensity, Roundness, InColor) : -1;
            },
            // (Color, Priority 기본값 사용) 6개 인수
            [](APlayerCameraManager* Self, int Idx, float InDuration, float Radius, float Softness, float Intensity, float Roundness) -> int
            {
                return Self ? Self->UpdateVignette(Idx, InDuration, Radius, Softness, Intensity, Roundness) : -1;
            }
        ),

        // --- AdjustVignette ---
        "AdjustVignette", sol::overload(
            // (Full) 7개 인수
            [](APlayerCameraManager* Self, float InDuration, float Radius, float Softness, float Intensity, float Roundness, const FLinearColor& InColor, int32 InPriority)
            {
                if (Self) Self->AdjustVignette(InDuration, Radius, Softness, Intensity, Roundness, InColor, InPriority);
            },
            // (Priority 기본값 사용) 6개 인수
            [](APlayerCameraManager* Self, float InDuration, float Radius, float Softness, float Intensity, float Roundness, const FLinearColor& InColor)
            {
                if (Self) Self->AdjustVignette(InDuration, Radius, Softness, Intensity, Roundness, InColor);
            },
            // (Color, Priority 기본값 사용) 5개 인수
            [](APlayerCameraManager* Self, float InDuration, float Radius, float Softness, float Intensity, float Roundness)
            {
                if (Self) Self->AdjustVignette(InDuration, Radius, Softness, Intensity, Roundness);
            }
        ),

        // --- DeleteVignette ---
        "DeleteVignette", [](APlayerCameraManager* Self)
        {
            if (Self) Self->DeleteVignette();
        },
            
        "SetViewTarget", [](APlayerCameraManager* self, LuaComponentProxy& Proxy)
        {
            // 타입 안정성 확인
            if (self && Proxy.Instance && Proxy.Class == UCameraComponent::StaticClass())
            {
                // 프록시에서 실제 컴포넌트 포인터 추출
                auto* CameraComp = static_cast<UCameraComponent*>(Proxy.Instance);
                self->SetViewCamera(CameraComp);
            }
        },

        "SetViewTargetWithBlend", [](APlayerCameraManager* self, LuaComponentProxy& Proxy, float InBlendTime)
        {
            // 타입 안정성 확인
            if (self && Proxy.Instance && Proxy.Class == UCameraComponent::StaticClass())
            {
                // 프록시에서 실제 컴포넌트 포인터 추출
                auto* CameraComp = static_cast<UCameraComponent*>(Proxy.Instance);
                self->SetViewCameraWithBlend(CameraComp, InBlendTime);
            }
        },

        // --- Gamma Correction ---
         // (Gamma Correction 기본값 사용) 1개 인수
        "StartGamma", [](APlayerCameraManager* Self, float Gamma)
        {
            if (Self)
            {
                Self->StartGamma(Gamma);
            }
        },

        // --- Depth of Field ---
        // (Full) 7개 인수
        "StartDOF", [](APlayerCameraManager* Self, float FocalDistance, float FocalRegion, float NearTransitionRegion, float FarTransitionRegion, float MaxNearBlurSize, float MaxFarBlurSize, int32 InPriority)
        {
            if (Self)
            {
                Self->StartDOF(FocalDistance, FocalRegion, NearTransitionRegion, FarTransitionRegion, MaxNearBlurSize, MaxFarBlurSize, InPriority);
            }
        }
    );
}

bool FLuaManager::LoadScriptInto(sol::environment& Env, const FString& Path) {
    auto Chunk = Lua->load_file(Path);
    if (!Chunk.valid()) { sol::error Err = Chunk; UE_LOG("[Lua][error] %s", Err.what()); return false; }
    
    sol::protected_function ProtectedFunc = Chunk;
    sol::set_environment(Env, ProtectedFunc);         
    auto Result = ProtectedFunc();
    if (!Result.valid()) { sol::error Err = Result; UE_LOG("[Lua][error] %s", Err.what()); return false; }
    return true;
}

void FLuaManager::Tick(double DeltaSeconds)
{
    CoroutineSchedular.Tick(DeltaSeconds);
}

void FLuaManager::ShutdownBeforeLuaClose()
{
    CoroutineSchedular.ShutdownBeforeLuaClose();
    
    FLuaBindRegistry::Get().Reset();
    
    SharedLib = sol::nil;
}

// Lua 함수 캐시 함수
sol::protected_function FLuaManager::GetFunc(sol::environment& Env, const char* Name)
{
    // (*Lua)[BeginPlay]()를 VM이 아닌, env에서 생성 및 캐시한다.
    // TODO : 함수 이름이 중복된다면?
    if (!Env.valid())
        return {};

    sol::object Object = Env[Name];
    
    if (Object == sol::nil || Object.get_type() != sol::type::function)
        return {};
    
    sol::protected_function Func = Object.as<sol::protected_function>();
    
    return Func;
}