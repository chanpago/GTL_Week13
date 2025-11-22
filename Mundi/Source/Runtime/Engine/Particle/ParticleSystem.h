#pragma once

class UParticleEmitter;
class UParticleModule;
namespace json { class JSON; }
using JSON = json::JSON;



class UParticleSystem : public UObject
{
public:
    void BuildRuntimeCache();

    // 저장
    // ParticleSystem을 JSON 형식으로 파일에 저장합니다
    // SerializeToJson()을 호출해서 JSON 데이터를 생성
    bool SaveToFile(const FString& FilePath);

    // 로드
    // 파일에서 JSON을 읽어서 새 ParticleSystem 객체를 생성
    // std::ifstream으로 파일 읽기
    // JSON 파싱
    // 새 ParticleSystem 생성 후 DeserializeFromJson() 호출
    static UParticleSystem* LoadFromFile(const FString& FilePath);

    // JSON 직렬화
    // ParticleSystem의 모든 데이터를 JSON 객체로 변환합니다
    // 저장하는 데이터:
    // Type: "ParticleSystem" (식별용)
    // Version: 1 (버전 관리)
    // Name: 파티클 시스템 이름
    // Emitters: 이미터 배열
    JSON SerializeToJson() const;

    // JSON 데이터를 읽어서 ParticleSystem 객체의 데이터를 채웁니다
    // 복원하는 데이터:
    // Name → ObjectName에 설정
    // Emitters 배열 → 각 이미터 생성하고 Emitters에 추가
    // 유효성 검사: Type이 "ParticleSystem"인지 확인
    // 마지막에 BuildRuntimeCache() 호출
    bool DeserializeFromJson(JSON& InJson);

private:
    // 헬퍼 함수
    JSON SerializeModule(UParticleModule* Module) const;
    UParticleModule* DeserializeModule(JSON& ModuleJson);

public:
    TArray<UParticleEmitter*> Emitters;
    int32 MaxActiveParticles = 0;
    float MaxLifetime = 0.f;
};