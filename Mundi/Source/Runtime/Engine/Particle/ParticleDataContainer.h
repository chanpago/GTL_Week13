#pragma once
FORCEINLINE uint32 AlignUp(uint32 Value, uint32 Alignment)
{
    return (Value + (Alignment - 1)) & ~(Alignment - 1);
}

// 렌더 스레드에서만 쓰는 단일 메모리 블록
struct FParticleDataContainer
{
    int32 MemBlockSize = 0;
    int32 ParticleDataNumBytes = 0;
    int32 ParticleIndicesNumShorts = 0;

    uint8* RawBlock = nullptr;
    uint8* ParticleData = nullptr; // 힙에 할당한 메모리 블록을 가리킨다.
    uint16* ParticleIndices = nullptr; // not allocated, this is at the end of the memory block

    void Allocate(int32 InParticleBytes, int32 InIndexCount)
    {
        const uint32 Alignment = 16;
        const uint32 ParticleSection = AlignUp(InParticleBytes, Alignment);
        const uint32 IndexSection = AlignUp(InIndexCount * sizeof(uint16), Alignment);

        MemBlockSize = ParticleSection + IndexSection;

        RawBlock = static_cast<uint8*>(FMemoryManager::Allocate(MemBlockSize, Alignment));
        ParticleDataNumBytes = InParticleBytes;
        ParticleIndicesNumShorts = static_cast<int32>(InIndexCount);

        ParticleData = RawBlock; // 앞부분
        ParticleIndices = reinterpret_cast<uint16*>(RawBlock + ParticleSection);
    }

    void Free()
    {
        if (RawBlock)
        {
            FMemoryManager::Deallocate(RawBlock);
            RawBlock = ParticleData = nullptr;
            ParticleIndices = nullptr;
        }
        ParticleDataNumBytes = 0;
        ParticleIndicesNumShorts = 0;
        MemBlockSize = 0;
    }
};
