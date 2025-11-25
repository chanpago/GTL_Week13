#pragma once
#include <cmath>        // ← 추가 (std::sin, std::cos, std::atan2, std::copysign 등)
#include <algorithm>
#include <string>
#include <limits>

#include "UEContainer.h"
#include "Archive.h"

// 혹시 다른 헤더에서 새어 들어온 매크로 방지
#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif
constexpr float KINDA_SMALL_NUMBER = 1e-6f;

// ─────────────────────────────
// Constants & Helpers
// ─────────────────────────────
constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = 6.2831853071795864769f;
constexpr float HALF_PI = 1.5707963267948966192f;

inline float DegreesToRadians(float Degree) { return Degree * (PI / 180.0f); }
inline float RadiansToDegrees(float Radian) { return Radian * (180.0f / PI); }
// FMath 네임스페이스 대체
namespace FMath
{
	template<typename T>
	static T Max(T A, T B) { return std::max(A, B); }

	template<typename T, typename...Ts>
	constexpr T Max(T first, Ts... rest)
	{
		((first = std::max(first, static_cast<T>(rest))), ...);
		return first;
	}

	template<typename T> 
	static T Sqrt(T A)
	{
		return std::sqrt(A);
	} 
	
	template<typename T>
	static T Min(T A, T B) { return std::min(A, B); }

	template<typename T>
	static T Abs(T Value) { return Value < 0 ? -Value : Value; }

	template<typename T>
	static T Clamp(T Value, T Min, T Max)
	{
		return Value < Min ? Min : (Value > Max ? Max : Value);
	}

	template<typename T>
	static T Lerp(T A, T B, float Alpha)
	{
		return A + (B - A) * Alpha;
	}

	/**
	 * @brief 내림 (소수점 이하 버림)
	 * @param Value 입력 값
	 * @return 내림된 실수 값 (예: 2.7f → 2.0f)
	 */
	static float Floor(float Value)
	{
		return std::floor(Value);
	}

	/**
	 * @brief 내림 후 정수로 변환
	 * @param Value 입력 값
	 * @return 내림된 정수 값 (예: 2.7f → 2)
	 */
	static int32 FloorToInt(float Value)
	{
		return static_cast<int32>(std::floor(Value));
	}

	/**
	 * @brief 올림 (소수점 이하 올림)
	 * @param Value 입력 값
	 * @return 올림된 실수 값 (예: 2.1f → 3.0f)
	 */
	static float Ceil(float Value)
	{
		return std::ceil(Value);
	}

	/**
	 * @brief 올림 후 정수로 변환
	 * @param Value 입력 값
	 * @return 올림된 정수 값 (예: 2.1f → 3)
	 */
	static int32 CeilToInt(float Value)
	{
		return static_cast<int32>(std::ceil(Value));
	}

	/**
	 * @brief 반올림 후 정수로 변환
	 * @param Value 입력 값
	 * @return 반올림된 정수 값 (예: 2.5f → 3, 2.4f → 2)
	 */
	static int32 RoundToInt(float Value)
	{
		return static_cast<int32>(std::round(Value));
	}

	/**
	 * @brief 부동소수점 나머지 연산
	 * @param X 피제수
	 * @param Y 제수
	 * @return X를 Y로 나눈 나머지 (예: Fmod(5.5f, 2.0f) = 1.5f)
	 */
	static float Fmod(float X, float Y)
	{
		return std::fmod(X, Y);
	}

	/**
	 * @brief 값이 0에 가까운지 확인
	 * @param Value 확인할 값
	 * @param Tolerance 허용 오차 (기본값: KINDA_SMALL_NUMBER)
	 * @return true면 거의 0, false면 0이 아님
	 */
	static bool IsNearlyZero(float Value, float Tolerance = KINDA_SMALL_NUMBER)
	{
		return Abs(Value) <= Tolerance;
	}

	/**
	 * @brief 두 값이 거의 같은지 확인
	 * @param A 첫 번째 값
	 * @param B 두 번째 값
	 * @param Tolerance 허용 오차 (기본값: KINDA_SMALL_NUMBER)
	 * @return true면 거의 같음, false면 다름
	 */
	static bool IsNearlyEqual(float A, float B, float Tolerance = KINDA_SMALL_NUMBER)
	{
		return Abs(A - B) <= Tolerance;
	}
}
// 각도를 -180 ~ 180 범위로 정규화 (모듈러 연산)
inline float NormalizeAngleDeg(float angleDeg)
{
	// fmod로 -360 ~ 360 범위로 만든 후, -180 ~ 180 범위로 조정
	angleDeg = std::fmod(angleDeg, 360.0f);
	if (angleDeg > 180.0f)
		angleDeg -= 360.0f;
	else if (angleDeg < -180.0f)
		angleDeg += 360.0f;
	return angleDeg;
}

// ─────────────────────────────
// Forward Declarations
// ─────────────────────────────
struct FVector;
struct FVector4;
struct FQuat;
struct FMatrix;
struct FTransform;
struct FAABB;

// ─────────────────────────────
// FVector (2D Vector)
// ─────────────────────────────

//// Add this global operator== for FVector2D and FVector4 to fix E0349 errors
//inline bool operator==(const FVector2D& A, const FVector2D& B)
//{
//    return std::fabs(A.X - B.X) < KINDA_SMALL_NUMBER &&
//        std::fabs(A.Y - B.Y) < KINDA_SMALL_NUMBER;
//}
//
//inline bool operator!=(const FVector2D& A, const FVector2D& B)
//{
//    return !(A == B);
//}
//
//inline bool operator==(const FVector4& A, const FVector4& B)
//{
//    return std::fabs(A.X - B.X) < KINDA_SMALL_NUMBER &&
//        std::fabs(A.Y - B.Y) < KINDA_SMALL_NUMBER &&
//        std::fabs(A.Z - B.Z) < KINDA_SMALL_NUMBER &&
//        std::fabs(A.W - B.W) < KINDA_SMALL_NUMBER;
//}
//
//inline bool operator!=(const FVector4& A, const FVector4& B)
//{
//    return !(A == B);
//}

struct FVector2D
{
	float X, Y;

	FVector2D(float InX = 0.0f, float InY = 0.0f) : X(InX), Y(InY)
	{
	}

	static FVector2D Zero()
	{
		return FVector2D(0.0f, 0.0f);
	}

	static FVector2D One()
	{
		return FVector2D(1.0f, 1.0f);
	}

	bool operator==(const FVector2D& V) const
	{
		return std::fabs(X - V.X) < KINDA_SMALL_NUMBER &&
			std::fabs(Y - V.Y) < KINDA_SMALL_NUMBER;
	}
	bool operator!=(const FVector2D& V) const { return !(*this == V); }

	FVector2D operator-(const FVector2D& Other) const
	{
		return FVector2D(X - Other.X, Y - Other.Y);
	}

	FVector2D operator+(const FVector2D& Other) const
	{
		return FVector2D(X + Other.X, Y + Other.Y);
	}

	FVector2D operator*(float Scalar) const
	{
		return FVector2D(X * Scalar, Y * Scalar);
	}

	float Length() const
	{
		return std::sqrt(X * X + Y * Y);
	}

	static FVector2D Lerp(const FVector2D& A, const FVector2D& B, float T)
	{
		return A + (B - A) * T;
	}

	FVector2D GetNormalized() const
	{
		float Len = Length();
		if (Len > 0.0001f)
			return FVector2D(X / Len, Y / Len);
		return FVector2D(0.0f, 0.0f);
	}

	// 직렬화 연산자
	friend FArchive& operator<<(FArchive& Ar, FVector2D& V)
	{
		Ar.Serialize(&V.X, sizeof(float));
		Ar.Serialize(&V.Y, sizeof(float));
		return Ar;
	}
};


// ─────────────────────────────
// FVector (3D Vector)
// ─────────────────────────────
struct FVector
{
	float X, Y, Z;

	FVector(float InX = 0, float InY = 0, float InZ = 0)
		: X(InX), Y(InY), Z(InZ)
	{
	}

	// 기본 연산
	FVector operator+(const FVector& V) const { return FVector(X + V.X, Y + V.Y, Z + V.Z); }
	FVector operator-(const FVector& V) const { return FVector(X - V.X, Y - V.Y, Z - V.Z); }
	FVector operator*(const FVector& V) const { return FVector(X * V.X, Y * V.Y, Z * V.Z); }
	FVector operator/(const FVector& V) const { return FVector(X / V.X, Y / V.Y, Z / V.Z); }
	FVector operator*(float S)        const { return FVector(X * S, Y * S, Z * S); }
	FVector operator/(float S)        const { return FVector(X / S, Y / S, Z / S); }
	FVector operator+(float S)        const { return FVector(X + S, Y + S, Z + S); }
	FVector operator-(float S)        const { return FVector(X - S, Y - S, Z - S); }
	FVector operator-()               const { return FVector(-X, -Y, -Z); }

	FVector& operator+=(const FVector& V) { X += V.X; Y += V.Y; Z += V.Z; return *this; }
	FVector& operator-=(const FVector& V) { X -= V.X; Y -= V.Y; Z -= V.Z; return *this; }
	FVector& operator*=(float S) { X *= S; Y *= S; Z *= S; return *this; }
	FVector& operator/=(float S) { X /= S; Y /= S; Z /= S; return *this; }
	FVector& operator+=(float S) { X += S; Y += S; Z += S; return *this; }
	FVector& operator-=(float S) { X -= S; Y -= S; Z -= S; return *this; }
	float& operator[](int Index)
	{
		assert(Index >= 0 && Index < 3); // 잘못된 인덱스면 프로그램 중단
		return ((&X)[Index]);
	}

	const float& operator[](int Index) const
	{
		assert(Index >= 0 && Index < 3);
		return ((&X)[Index]);
	}
	bool operator==(const FVector& V) const
	{
		return std::fabs(X - V.X) < KINDA_SMALL_NUMBER &&
			std::fabs(Y - V.Y) < KINDA_SMALL_NUMBER &&
			std::fabs(Z - V.Z) < KINDA_SMALL_NUMBER;
	}
	bool operator!=(const FVector& V) const { return !(*this == V); }

	FVector ComponentMin(const FVector& B)
	{
		return FVector(
			(X < B.X) ? X : B.X,
			(Y < B.Y) ? Y : B.Y,
			(Z < B.Z) ? Z : B.Z
		);
	}
	FVector ComponentMax(const FVector& B)
	{
		return FVector(
			(X > B.X) ? X : B.X,
			(Y > B.Y) ? Y : B.Y,
			(Z > B.Z) ? Z : B.Z
		);
	}

	// 크기
	float Size()         const { return std::sqrt(X * X + Y * Y + Z * Z); }
	float SizeSquared()  const { return X * X + Y * Y + Z * Z; }

	// XYZ중 가장 큰 값 리턴
	float GetMaxValue() const{ return (X > Y) ? ((X > Z) ? X : Z) : Y; }

	// 정규화
	FVector GetNormalized() const
	{
		float S = Size();
		return (S > KINDA_SMALL_NUMBER) ? (*this / S) : FVector(0, 0, 0);
	}
	void Normalize()
	{
		float S = Size();
		if (S > KINDA_SMALL_NUMBER) { X /= S; Y /= S; Z /= S; }
	}
	FVector GetSafeNormal() const { return GetNormalized(); }

	// 내적/외적
	static float   Dot(const FVector& A, const FVector& B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
	static FVector Cross(const FVector& A, const FVector& B)
	{
		return FVector(
			A.Y * B.Z - A.Z * B.Y,
			A.Z * B.X - A.X * B.Z,
			A.X * B.Y - A.Y * B.X
		);
	}

	// 보조 유틸
	static FVector Lerp(const FVector& A, const FVector& B, float T)
	{
		return A + (B - A) * T;
	}
	static float Distance(const FVector& A, const FVector& B)
	{
		return (B - A).Size();
	}
	static float AngleBetween(const FVector& A, const FVector& B) // radians
	{
		float D = Dot(A, B) / (std::sqrt(A.SizeSquared() * B.SizeSquared()) + KINDA_SMALL_NUMBER);
		D = std::max(-1.0f, std::min(1.0f, D));
		return std::acos(D);
	}
	static FVector Project(const FVector& A, const FVector& OnNormal)
	{
		FVector N = OnNormal.GetNormalized();
		return N * Dot(A, N);
	}
	static FVector Reflect(const FVector& V, const FVector& Normal) // Normal normalized
	{
		return V - Normal * (2.0f * Dot(V, Normal));
	}
	static FVector Clamp(const FVector& V, float minLen, float maxLen)
	{
		float Length = V.Size();
		if (Length < KINDA_SMALL_NUMBER) return FVector(0, 0, 0);
		float Value = std::max(minLen, std::min(maxLen, Length));
		return V * (Value / Length);
	}
    static FVector Zero()
    {
		return FVector(0.0f, 0.0f, 0.0f);
    }
        

	static FVector One()
	{
		return FVector(1.f, 1.f, 1.f);
	}

	// 영 벡터 체크
	bool IsZero() const
	{
		return std::fabs(X) < KINDA_SMALL_NUMBER &&
			std::fabs(Y) < KINDA_SMALL_NUMBER &&
			std::fabs(Z) < KINDA_SMALL_NUMBER;
	}

	/*FVector4 ToFVector4() const
	{
		return FVector4(X, Y, Z, 1.0f);
	}*/
	FVector SnapToGrid(const FVector& GridSize, bool bFloor) const
	{
		float _X = 0, _Y = 0, _Z = 0;
		if (bFloor)
		{
			_X = GridSize.X != 0 ? floorf(X / GridSize.X) * GridSize.X : X;
			_Y = GridSize.Y != 0 ? floorf(Y / GridSize.Y) * GridSize.Y : Y;			
			_Z = GridSize.Z != 0 ? floorf(Z / GridSize.Z) * GridSize.Z : Z;			
		}
		else
		{
			_X = GridSize.X != 0 ? ceilf(X / GridSize.X) * GridSize.X : X;
			_Y = GridSize.Y != 0 ? ceilf(Y / GridSize.Y) * GridSize.Y : Y;
			_Z = GridSize.Z != 0 ? ceilf(Z / GridSize.Z) * GridSize.Z : Z;
		}
		return FVector(_X, _Y, _Z);
	}
	void Log();

	// 직렬화 연산자
	friend FArchive& operator<<(FArchive& Ar, FVector& V)
	{
		Ar.Serialize(&V.X, sizeof(float));
		Ar.Serialize(&V.Y, sizeof(float));
		Ar.Serialize(&V.Z, sizeof(float));
		return Ar;
	}
};

// ─────────────────────────────
// FVector4 (4D Vector)
// ─────────────────────────────
struct alignas(16) FVector4
{
	union
	{
		__m128 SimdData;
		struct { float X, Y, Z, W; };
	};

	FVector4(float InX = 0.0f, float InY = 0.0f, float InZ = 0.0f, float InW = 0.0f)
	{
		SimdData = _mm_set_ps(InW, InZ, InY, InX);
	}

	FVector4(const __m128& InSimd) : SimdData(InSimd) {}

	FVector4 operator+(const FVector4& V) const { return FVector4(_mm_add_ps(SimdData, V.SimdData)); }
	FVector4 operator-(const FVector4& V) const { return FVector4(_mm_sub_ps(SimdData, V.SimdData)); }
	FVector4 operator*(float S) const { return FVector4(_mm_mul_ps(SimdData, _mm_set1_ps(S))); }
	FVector4 operator/(float S) const { return FVector4(_mm_div_ps(SimdData, _mm_set1_ps(S))); }

	FVector4& operator+=(const FVector4& V) { SimdData = _mm_add_ps(SimdData, V.SimdData); return *this; }
	FVector4& operator-=(const FVector4& V) { SimdData = _mm_sub_ps(SimdData, V.SimdData); return *this; }
	FVector4& operator*=(float S) { SimdData = _mm_mul_ps(SimdData, _mm_set1_ps(S)); return *this; }
	FVector4& operator/=(float S) { SimdData = _mm_div_ps(SimdData, _mm_set1_ps(S)); return *this; }

	bool operator==(const FVector4& V) const
	{
		return std::fabs(X - V.X) < KINDA_SMALL_NUMBER &&
			std::fabs(Y - V.Y) < KINDA_SMALL_NUMBER &&
			std::fabs(Z - V.Z) < KINDA_SMALL_NUMBER &&
			std::fabs(W - V.W) < KINDA_SMALL_NUMBER;

	}

	bool operator!=(const FVector4& Other) const { return !((*this) == Other); }

	FVector4 ComponentMin(const FVector4& B) const
	{
		return FVector4(_mm_min_ps(this->SimdData, B.SimdData));
	}
	FVector4 ComponentMax(const FVector4& B) const
	{
		return FVector4(_mm_max_ps(this->SimdData, B.SimdData));
	}
	
	/** FVector(Point)를 FVector4(Point, W=1)로 변환합니다. */
	static FVector4 FromPoint(const FVector& P)
	{
		return FVector4(P.X, P.Y, P.Z, 1.0f);
	}

	/** FVector(Direction)를 FVector4(Direction, W=0)로 변환합니다. */
	static FVector4 FromDirection(const FVector& D)
	{
		return FVector4(D.X, D.Y, D.Z, 0.0f);
	}

	// 직렬화 연산자
	friend FArchive& operator<<(FArchive& Ar, FVector4& V)
	{
		Ar.Serialize(&V.X, sizeof(float));
		Ar.Serialize(&V.Y, sizeof(float));
		Ar.Serialize(&V.Z, sizeof(float));
		Ar.Serialize(&V.W, sizeof(float));
		return Ar;
	}

};

// Quaternion 정규화(+w 기준 캐논화)
inline void NormalizeQuat(float& x, float& y, float& z, float& w)
{
	const float n = std::sqrt(x * x + y * y + z * z + w * w);
	if (n > 1e-12) { x /= n; y /= n; z /= n; w /= n; }
	if (w < 0.0) { x = -x; y = -y; z = -z; w = -w; } // q와 -q 동치 → 표준화
}

// ─────────────────────────────
// FQuat (Quaternion)
// ─────────────────────────────
struct FQuat
{
	float X, Y, Z, W;

	FQuat(float InX = 0, float InY = 0, float InZ = 0, float InW = 1)
		: X(InX), Y(InY), Z(InZ), W(InW)
	{
	}
	FQuat(const FMatrix& M);

	static FQuat Identity() { return FQuat(0, 0, 0, 1); }

	// 곱 (회전 누적)
	FQuat operator*(const FQuat& Q) const
	{
		return FQuat(
			W * Q.X + X * Q.W + Y * Q.Z - Z * Q.Y,
			W * Q.Y - X * Q.Z + Y * Q.W + Z * Q.X,
			W * Q.Z + X * Q.Y - Y * Q.X + Z * Q.W,
			W * Q.W - X * Q.X - Y * Q.Y - Z * Q.Z
		);
	}

	static float Dot(const FQuat& A, const FQuat& B)
	{
		return A.X * B.X + A.Y * B.Y + A.Z * B.Z + A.W * B.W;
	}

	float SizeSquared() const { return X * X + Y * Y + Z * Z + W * W; }
	float Size()        const { return std::sqrt(SizeSquared()); }

	void Normalize()
	{
		float S = Size();
		if (S > KINDA_SMALL_NUMBER) { X /= S; Y /= S; Z /= S; W /= S; }
		else { *this = Identity(); }
	}

	FQuat GetNormalized() const
	{
		FQuat Quat = *this; Quat.Normalize(); return Quat;
	}

	FQuat Conjugate() const { return FQuat(-X, -Y, -Z, W); }
	FQuat Inverse()   const
	{
		float SS = SizeSquared();
		if (SS <= KINDA_SMALL_NUMBER) return Identity();
		return Conjugate() * (1.0f / SS);
	}
	// 스칼라 곱
	FQuat operator*(float S) const { return FQuat(X * S, Y * S, Z * S, W * S); }

	// 벡터 회전
	FVector RotateVector(const FVector& V) const;

	// Axis-Angle → Quaternion (axis normalized, angle in radians)
	static FQuat FromAxisAngle(const FVector& Axis, float Angle)
	{
		float Half = Angle * 0.5f;
		float S = std::sin(Half);
		FVector N = Axis.GetNormalized();
		return FQuat(N.X * S, N.Y * S, N.Z * S, std::cos(Half));
	}

	// 오일러 → 쿼터니언 (Pitch=X, Yaw=Y, Roll=Z in degrees)
	// ZYX 순서 (Roll → Yaw → Pitch) - 로컬 축 회전
	static FQuat MakeFromEulerZYX(const FVector& EulerDeg)
	{
		const float RollRad = DegreesToRadians(EulerDeg.X); // Roll about X
		const float PitchRad = DegreesToRadians(EulerDeg.Y); // Pitch about Y
		const float YawRad = DegreesToRadians(EulerDeg.Z); // Yaw about Z

		const float PR = RollRad * 0.5f;
		const float PP = PitchRad * 0.5f;
		const float PY = YawRad * 0.5f;

		const float CR = cosf(PR), SR = sinf(PR);
		const float CP = cosf(PP), SP = sinf(PP);
		const float CY = cosf(PY), SY = sinf(PY);

		// 2. 올바른 회전 순서: Q = Q_Yaw * Q_Pitch * Q_Roll (Qz * Qy * Qx)
		FQuat Quat;
		Quat.X = SR * CP * CY - CR * SP * SY;
		Quat.Y = CR * SP * CY + SR * CP * SY;
		Quat.Z = CR * CP * SY - SR * SP * CY;
		Quat.W = CR * CP * CY + SR * SP * SY;

		// (결과 쿼터니언은 이미 정규화되어 있으므로 GetNormalized()는 
		//  부동 소수점 오차 보정 외에는 수학적으로 불필요합니다.)
		return Quat;
	}

	/**
	 * @brief 쿼터니언을 ZYX 회전 순서의 오일러 각(디그리)으로 변환합니다.
	 * @return FVector(Roll(X), Pitch(Y), Yaw(Z)) 형태의 오일러 각 (단위: Degree)
	 */
	FVector ToEulerZYXDeg() const
	{
		// 멤버 변수의 로컬 복사본임을 명확히 하기 위해 'Local' 접두사 사용
		float LocalX = this->X;
		float LocalY = this->Y;
		float LocalZ = this->Z;
		float LocalW = this->W;

		// 정규화 (w가 양수가 되도록)
		const float Norm = std::sqrt(LocalX * LocalX + LocalY * LocalY + LocalZ * LocalZ + LocalW * LocalW);
		if (Norm > KINDA_SMALL_NUMBER)
		{
			const float InvNorm = 1.0f / Norm;
			LocalX *= InvNorm;
			LocalY *= InvNorm;
			LocalZ *= InvNorm;
			LocalW *= InvNorm;
		}

		if (LocalW < 0.0f)
		{
			LocalX = -LocalX;
			LocalY = -LocalY;
			LocalZ = -LocalZ;
			LocalW = -LocalW;
		}

		// 회전 행렬 성분 계산
		const float R20 = 2.0f * (LocalX * LocalZ - LocalY * LocalW);

		// 짐벌 락을 고려한 ZYX 분해
		const float SY = FMath::Clamp(-R20, -1.0f, 1.0f);
		float Pitch = std::asin(SY);
		float Roll, Yaw;

		if (std::abs(std::cos(Pitch)) > KINDA_SMALL_NUMBER)
		{
			const float R21 = 2.0f * (LocalY * LocalZ + LocalX * LocalW);
			const float R22 = 1.0f - 2.0f * (LocalX * LocalX + LocalY * LocalY);
			Roll = std::atan2(R21, R22);

			const float R10 = 2.0f * (LocalX * LocalY + LocalZ * LocalW);
			const float R00 = 1.0f - 2.0f * (LocalY * LocalY + LocalZ * LocalZ);
			Yaw = std::atan2(R10, R00);
		}
		else // 짐벌 락 발생 시
		{
			Roll = 0.0f;
			const float R01 = 2.0f * (LocalX * LocalY - LocalZ * LocalW);
			const float R02 = 2.0f * (LocalX * LocalZ + LocalY * LocalW);
			Yaw = std::atan2(-R01, R02);
		}

		return FVector(
			RadiansToDegrees(Roll),
			RadiansToDegrees(Pitch),
			RadiansToDegrees(Yaw)
		);
	}

	// 두 단위 벡터(A -> B) 사이의 최단 회전 쿼터니언을 구함
	static FQuat FindBetweenNormals(const FVector& A, const FVector& B)
	{
		float Dot = FVector::Dot(A, B);
		// Case A: 두 벡터가 이미 거의 같은 방향일 때
		if (Dot > 0.9999f)
		{
			return Identity();
		}

		// Case B: 두 벡터가 정반대 방향일 때 (180도 회전)
		if (Dot < -0.9999f)
		{
			// A와 수직인 임의의 회전축을 찾아야 함
			// Z축(0,0,1)과 외적 시도
			FVector Axis = FVector::Cross({0,0,1}, A);
			
			// 만약 A가 Z축과 평행해서 외적 결과가 0이라면, X축(1,0,0)과 외적 시도
			if (Axis.SizeSquared() < 0.001f)
			{
				// Cross( (1,0,0), A )
				Axis.X = 0.0f;
				Axis.Y = -A.Z;
				Axis.Z = A.Y;
			}
        
			Axis.Normalize();
        
			// 180도 회전 쿼터니언: (x, y, z, 0)
			return {Axis.X, Axis.Y, Axis.Z, 0.0f};
		}

		// Case C: 일반적인 경우
		// 회전축 = A Cross B
		FVector Cross = FVector::Cross(A, B);

		// 쿼터니언 생성 공식 (Half-Way Vector Trick)
		// q = [ Cross, 1 + Dot ] -> 이후 정규화
		FQuat Q(Cross.X, Cross.Y, Cross.Z, 1.0f + Dot);
		Q.Normalize();
		return Q;
	}

	FVector GetForwardVector() const
	{
		// 보통 게임엔진(Z-Up, Forward = +X) 기준
		return RotateVector(FVector(1, 0, 0));
	}

	FVector GetRightVector() const
	{
		return RotateVector(FVector(0, 1, 0));
	}

	FVector GetUpVector() const
	{
		return RotateVector(FVector(0, 0, 1));
	}

	// Slerp
	static FQuat Slerp(const FQuat& A, const FQuat& B, float T)
	{
		float CosTheta = Dot(A, B);
		FQuat End = B;

		// 가장 짧은 호
		if (CosTheta < 0.0f) { End = FQuat(-B.X, -B.Y, -B.Z, -B.W); CosTheta = -CosTheta; }

		// 근접하면 Nlerp
		const float SLERP_EPS = 1e-3f;
		if (CosTheta > 1.0f - SLERP_EPS)
		{
			FQuat r = FQuat(
				A.X + (End.X - A.X) * T,
				A.Y + (End.Y - A.Y) * T,
				A.Z + (End.Z - A.Z) * T,
				A.W + (End.W - A.W) * T
			);
			r.Normalize();
			return r;
		}

		float Theta = std::acos(CosTheta);
		float SinTheta = std::sin(Theta);
		float W1 = std::sin((1.0f - T) * Theta) / SinTheta;
		float W2 = std::sin(T * Theta) / SinTheta;

		FQuat Quat = A * W1 + End * W2;
		Quat.Normalize();
		return Quat;
	}

	// 보조: 선형 보간 후 정규화
	static FQuat Nlerp(const FQuat& A, const FQuat& B, float T)
	{
		FQuat End = (Dot(A, B) < 0.0f) ? FQuat(-B.X, -B.Y, -B.Z, -B.W) : B;
		FQuat Quat(A.X + (End.X - A.X) * T,
			A.Y + (End.Y - A.Y) * T,
			A.Z + (End.Z - A.Z) * T,
			A.W + (End.W - A.W) * T);
		Quat.Normalize();
		return Quat;
	}

	// 비교 연산자
	bool operator==(const FQuat& Q) const
	{
		return std::fabs(X - Q.X) < KINDA_SMALL_NUMBER &&
			std::fabs(Y - Q.Y) < KINDA_SMALL_NUMBER &&
			std::fabs(Z - Q.Z) < KINDA_SMALL_NUMBER &&
			std::fabs(W - Q.W) < KINDA_SMALL_NUMBER;
	}
	bool operator!=(const FQuat& Q) const { return !(*this == Q); }

	// 단위 쿼터니온 체크
	bool IsIdentity() const
	{
		return std::fabs(X) < KINDA_SMALL_NUMBER &&
			std::fabs(Y) < KINDA_SMALL_NUMBER &&
			std::fabs(Z) < KINDA_SMALL_NUMBER &&
			std::fabs(W - 1.0f) < KINDA_SMALL_NUMBER;
	}

	// 선언: 행렬 변환
	FMatrix ToMatrix() const;

private:
	// 보조 연산 (내부용)
	FQuat operator+(const FQuat& Quat) const { return FQuat(X + Quat.X, Y + Quat.Y, Z + Quat.Z, W + Quat.W); }
};

// 스칼라 곱(전역)
inline FQuat operator*(float Scalar, const FQuat& Quat) { return FQuat(Quat.X * Scalar, Quat.Y * Scalar, Quat.Z * Scalar, Quat.W * Scalar); }

// ─────────────────────────────
// FMatrix (4x4 Matrix)
// (Row-major, Translation in M[row][3])
// ─────────────────────────────
struct alignas(16) FMatrix
{
	union
	{
		__m128 Rows[4];
		float M[4][4];
		FVector4 VRows[4];
	};

	FMatrix()
	{
		Rows[0] = _mm_setzero_ps();
		Rows[1] = _mm_setzero_ps();
		Rows[2] = _mm_setzero_ps();
		Rows[3] = _mm_setzero_ps();
	}

	FMatrix(const __m128& R0, const __m128& R1, const __m128& R2, const __m128& R3)
	{
		Rows[0] = R0;
		Rows[1] = R1;
		Rows[2] = R2;
		Rows[3] = R3;
	}

	FMatrix(float M00, float M01, float M02, float M03,
		float M10, float M11, float M12, float M13,
		float M20, float M21, float M22, float M23,
		float M30, float M31, float M32, float M33)
	{
		Rows[0] = _mm_set_ps(M03, M02, M01, M00);
		Rows[1] = _mm_set_ps(M13, M12, M11, M10);
		Rows[2] = _mm_set_ps(M23, M22, M21, M20);
		Rows[3] = _mm_set_ps(M33, M32, M31, M30);
	}

	static FMatrix Identity()
	{
		return FMatrix(
			1, 0, 0, 0,
			0, 1, 0, 0,
			0, 0, 1, 0,
			0, 0, 0, 1
		);
	}

	const static FMatrix ZUpToYUp;
	const static FMatrix YUpToZUp;
	// 비균일 스케일
	static FMatrix MakeScale(const FVector& S)
	{
		FMatrix R = Identity();
		R.M[0][0] = S.X;  // x' = x*Sx
		R.M[1][1] = S.Y;  // y' = y*Sy
		R.M[2][2] = S.Z;  // z' = z*Sz
		return R;
	}
	// 균일 스케일
	static FMatrix MakeScale(float S)
	{
		return MakeScale(FVector{ S, S, S });
	}
	// 평행이동 (row-vector에서는 마지막 "행"의 xyz가 이동)
	static FMatrix MakeTranslation(const FVector& T)
	{
		FMatrix R = Identity();
		// p' = [x y z 1] * M  =>  x' = ... + 1*M[3][0]  (따라서 M[3][0..2]에 T)
		R.M[3][0] = T.X;
		R.M[3][1] = T.Y;
		R.M[3][2] = T.Z;
		return R;
	}

	// SIMD-accelerated transpose
	FMatrix Transpose() const
	{
		__m128 temp0 = _mm_shuffle_ps(Rows[0], Rows[1], _MM_SHUFFLE(1, 0, 1, 0));
		__m128 temp1 = _mm_shuffle_ps(Rows[0], Rows[1], _MM_SHUFFLE(3, 2, 3, 2));
		__m128 temp2 = _mm_shuffle_ps(Rows[2], Rows[3], _MM_SHUFFLE(1, 0, 1, 0));
		__m128 temp3 = _mm_shuffle_ps(Rows[2], Rows[3], _MM_SHUFFLE(3, 2, 3, 2));

		FMatrix Result;
		Result.Rows[0] = _mm_shuffle_ps(temp0, temp2, _MM_SHUFFLE(2, 0, 2, 0));
		Result.Rows[1] = _mm_shuffle_ps(temp0, temp2, _MM_SHUFFLE(3, 1, 3, 1));
		Result.Rows[2] = _mm_shuffle_ps(temp1, temp3, _MM_SHUFFLE(2, 0, 2, 0));
		Result.Rows[3] = _mm_shuffle_ps(temp1, temp3, _MM_SHUFFLE(3, 1, 3, 1));
		return Result;
	}

	// Affine 역행렬 (마지막 행 = [0,0,0,1] 가정)
	FMatrix InverseAffine() const
	{
		// 상단 3x3 역행렬
		float A00 = M[0][0], A01 = M[0][1], A02 = M[0][2];
		float A10 = M[1][0], A11 = M[1][1], A12 = M[1][2];
		float A20 = M[2][0], A21 = M[2][1], A22 = M[2][2];

		float Det = A00 * (A11 * A22 - A12 * A21) - A01 * (A10 * A22 - A12 * A20) + A02 * (A10 * A21 - A11 * A20);
		if (std::fabs(Det) < KINDA_SMALL_NUMBER) return Identity();
		float InvDet = 1.0f / Det;

		// Rinv = R^{-1}
		FMatrix InvMat = Identity();
		InvMat.M[0][0] = (A11 * A22 - A12 * A21) * InvDet;
		InvMat.M[0][1] = -(A01 * A22 - A02 * A21) * InvDet;
		InvMat.M[0][2] = (A01 * A12 - A02 * A11) * InvDet;

		InvMat.M[1][0] = -(A10 * A22 - A12 * A20) * InvDet;
		InvMat.M[1][1] = (A00 * A22 - A02 * A20) * InvDet;
		InvMat.M[1][2] = -(A00 * A12 - A02 * A10) * InvDet;

		InvMat.M[2][0] = (A10 * A21 - A11 * A20) * InvDet;
		InvMat.M[2][1] = -(A00 * A21 - A01 * A20) * InvDet;
		InvMat.M[2][2] = (A00 * A11 - A01 * A10) * InvDet;

		// t: last row
		const FVector T(M[3][0], M[3][1], M[3][2]);

		// invT = -t * Rinv  (t treated as row-vector)
		FVector invT(
			-(T.X * InvMat.M[0][0] + T.Y * InvMat.M[1][0] + T.Z * InvMat.M[2][0]),
			-(T.X * InvMat.M[0][1] + T.Y * InvMat.M[1][1] + T.Z * InvMat.M[2][1]),
			-(T.X * InvMat.M[0][2] + T.Y * InvMat.M[1][2] + T.Z * InvMat.M[2][2])
		);

		FMatrix Out = Identity();
		// place Rinv in upper-left
		Out.M[0][0] = InvMat.M[0][0]; Out.M[0][1] = InvMat.M[0][1]; Out.M[0][2] = InvMat.M[0][2];
		Out.M[1][0] = InvMat.M[1][0]; Out.M[1][1] = InvMat.M[1][1]; Out.M[1][2] = InvMat.M[1][2];
		Out.M[2][0] = InvMat.M[2][0]; Out.M[2][1] = InvMat.M[2][1]; Out.M[2][2] = InvMat.M[2][2];
		// translation in last row
		Out.M[3][0] = invT.X; Out.M[3][1] = invT.Y; Out.M[3][2] = invT.Z; Out.M[3][3] = 1.0f;
		return Out;
	}
	// 회전행렬 
	FMatrix InverseAffineFast() const
	{
		// 상단 3x3
		const float A00 = M[0][0], A01 = M[0][1], A02 = M[0][2];
		const float A10 = M[1][0], A11 = M[1][1], A12 = M[1][2];
		const float A20 = M[2][0], A21 = M[2][1], A22 = M[2][2];

		// 오르소노멀(순수 회전) 빠른 체크: 행(또는 열) 직교 & 단위길이
		auto dot = [](float x0, float y0, float z0, float x1, float y1, float z1)
			{
				return x0 * x1 + y0 * y1 + z0 * z1;
			};
		auto len2 = [&](float x, float y, float z) { return dot(x, y, z, x, y, z); };

		const float e = 1e-4f; // 허용 오차
		const bool ortho =
			std::fabs(len2(A00, A01, A02) - 1.f) < e &&
			std::fabs(len2(A10, A11, A12) - 1.f) < e &&
			std::fabs(len2(A20, A21, A22) - 1.f) < e &&
			std::fabs(dot(A00, A01, A02, A10, A11, A12)) < e &&
			std::fabs(dot(A00, A01, A02, A20, A21, A22)) < e &&
			std::fabs(dot(A10, A11, A12, A20, A21, A22)) < e;

		FMatrix Out = Identity();
		const FVector t(M[3][0], M[3][1], M[3][2]);

		if (ortho)
		{
			// R^T
			Out.M[0][0] = A00; Out.M[0][1] = A10; Out.M[0][2] = A20;
			Out.M[1][0] = A01; Out.M[1][1] = A11; Out.M[1][2] = A21;
			Out.M[2][0] = A02; Out.M[2][1] = A12; Out.M[2][2] = A22;

			// invT = -t * R^T  (행벡터 기준)
			Out.M[3][0] = -(t.X * Out.M[0][0] + t.Y * Out.M[1][0] + t.Z * Out.M[2][0]);
			Out.M[3][1] = -(t.X * Out.M[0][1] + t.Y * Out.M[1][1] + t.Z * Out.M[2][1]);
			Out.M[3][2] = -(t.X * Out.M[0][2] + t.Y * Out.M[1][2] + t.Z * Out.M[2][2]);
			Out.M[3][3] = 1.f;
			return Out;
		}

		// 폴백: 일반 3x3 역행렬 (네가 올린 기존 코드와 동일 로직)
		return InverseAffine(); // = 너의 일반식 구현 호출
	}

	static FMatrix FromTRS(const FVector& T, const FQuat& R, const FVector& S)
	{
		FMatrix rot = R.ToMatrix();
		// scale 적용
		rot.M[0][0] *= S.X; rot.M[0][1] *= S.X; rot.M[0][2] *= S.X;
		rot.M[1][0] *= S.Y; rot.M[1][1] *= S.Y; rot.M[1][2] *= S.Y;
		rot.M[2][0] *= S.Z; rot.M[2][1] *= S.Z; rot.M[2][2] *= S.Z;
		// translation
		rot.M[3][0] = T.X;
		rot.M[3][1] = T.Y;
		rot.M[3][2] = T.Z;
		rot.M[3][3] = 1;
		return rot;
	}

	FMatrix Inverse() const
	{
		FMatrix InverseMatrix;
		// 원본 행렬(VP_Matrix)의 요소를 로컬 변수로 복사
		float m00 = this->M[0][0], m01 = this->M[0][1], m02 = this->M[0][2], m03 = this->M[0][3];
		float m10 = this->M[1][0], m11 = this->M[1][1], m12 = this->M[1][2], m13 = this->M[1][3];
		float m20 = this->M[2][0], m21 = this->M[2][1], m22 = this->M[2][2], m23 = this->M[2][3];
		float m30 = this->M[3][0], m31 = this->M[3][1], m32 = this->M[3][2], m33 = this->M[3][3];

		// --- 수반 행렬(Adjugate Matrix)의 첫 번째 행 계산 ---
		FMatrix Adj;

		// 2x2 부분 행렬식(sub-determinant) 계산
		float t0 = m22 * m33 - m23 * m32;
		float t1 = m21 * m33 - m23 * m31;
		float t2 = m21 * m32 - m22 * m31;
		float t3 = m20 * m33 - m23 * m30;
		float t4 = m20 * m32 - m22 * m30;
		float t5 = m20 * m31 - m21 * m30;

		Adj.M[0][0] = (m11 * t0 - m12 * t1 + m13 * t2);
		Adj.M[0][1] = -(m10 * t0 - m12 * t3 + m13 * t4);
		Adj.M[0][2] = (m10 * t1 - m11 * t3 + m13 * t5);
		Adj.M[0][3] = -(m10 * t2 - m11 * t4 + m12 * t5);

		// --- 행렬식(Determinant) 계산 ---
		float Det = m00 * Adj.M[0][0] + m01 * Adj.M[0][1] + m02 * Adj.M[0][2] + m03 * Adj.M[0][3];

		// 행렬식이 0에 가까우면 (특이 행렬), 역행렬이 존재하지 않음
		if (std::fabs(Det) < KINDA_SMALL_NUMBER)
		{
			return Identity(); // 단위 행렬 할당
		}
		else
		{
			// 행렬식의 역수 (1.0f / Det)
			float InvDet = 1.0f / Det;

			// --- 수반 행렬의 나머지 3개 행 계산 ---

			// 2x2 부분 행렬식 (Row 1)
			float t6 = m02 * m33 - m03 * m32;
			float t7 = m01 * m33 - m03 * m31;
			float t8 = m01 * m32 - m02 * m31;
			float t9 = m00 * m33 - m03 * m30;
			float t10 = m00 * m32 - m02 * m30;
			float t11 = m00 * m31 - m01 * m30;

			Adj.M[1][0] = -(m01 * t0 - m02 * t1 + m03 * t2); // C[0][1]
			Adj.M[1][1] = (m00 * t0 - m02 * t3 + m03 * t4); // C[1][1]
			Adj.M[1][2] = -(m00 * t1 - m01 * t3 + m03 * t5); // C[2][1]
			Adj.M[1][3] = (m00 * t2 - m01 * t4 + m02 * t5); // C[3][1]

			// 2x2 부분 행렬식 (Row 2)
			float t12 = m02 * m13 - m03 * m12;
			float t13 = m01 * m13 - m03 * m11;
			float t14 = m01 * m12 - m02 * m11;
			float t15 = m00 * m13 - m03 * m10;
			float t16 = m00 * m12 - m02 * m10;
			float t17 = m00 * m11 - m01 * m10;

			Adj.M[2][0] = (m31 * t12 - m32 * t13 + m33 * t14); // C[0][2]
			Adj.M[2][1] = -(m30 * t12 - m32 * t15 + m33 * t16); // C[1][2]
			Adj.M[2][2] = (m30 * t13 - m31 * t15 + m33 * t17); // C[2][2]
			Adj.M[2][3] = -(m30 * t14 - m31 * t16 + m32 * t17); // C[3][2]

			// 2x2 부분 행렬식 (Row 3)
			float t18 = m02 * m23 - m03 * m22;
			float t19 = m01 * m23 - m03 * m21;
			float t20 = m01 * m22 - m02 * m21;
			float t21 = m00 * m23 - m03 * m20;
			float t22 = m00 * m22 - m02 * m20;
			float t23 = m00 * m21 - m01 * m20;

			Adj.M[3][0] = -(m21 * t12 - m22 * t13 + m23 * t14); // C[0][3]
			Adj.M[3][1] = (m20 * t12 - m22 * t15 + m23 * t16); // C[1][3]
			Adj.M[3][2] = -(m20 * t13 - m21 * t15 + m23 * t17); // C[2][3]
			Adj.M[3][3] = (m20 * t14 - m21 * t16 + m22 * t17); // C[3][3]

			// --- 최종 역행렬 계산 ---
			InverseMatrix.M[0][0] = Adj.M[0][0] * InvDet;
			InverseMatrix.M[0][1] = Adj.M[1][0] * InvDet; // [0][1] <- [1][0]
			InverseMatrix.M[0][2] = Adj.M[2][0] * InvDet; // [0][2] <- [2][0]
			InverseMatrix.M[0][3] = Adj.M[3][0] * InvDet; // [0][3] <- [3][0]

			InverseMatrix.M[1][0] = Adj.M[0][1] * InvDet; // [1][0] <- [0][1]
			InverseMatrix.M[1][1] = Adj.M[1][1] * InvDet;
			InverseMatrix.M[1][2] = Adj.M[2][1] * InvDet; // [1][2] <- [2][1]
			InverseMatrix.M[1][3] = Adj.M[3][1] * InvDet; // [1][3] <- [3][1]

			InverseMatrix.M[2][0] = Adj.M[0][2] * InvDet; // [2][0] <- [0][2]
			InverseMatrix.M[2][1] = Adj.M[1][2] * InvDet; // [2][1] <- [1][2]
			InverseMatrix.M[2][2] = Adj.M[2][2] * InvDet;
			InverseMatrix.M[2][3] = Adj.M[3][2] * InvDet; // [2][3] <- [3][2]

			InverseMatrix.M[3][0] = Adj.M[0][3] * InvDet; // [3][0] <- [0][3]
			InverseMatrix.M[3][1] = Adj.M[1][3] * InvDet; // [3][1] <- [1][3]
			InverseMatrix.M[3][2] = Adj.M[2][3] * InvDet; // [3][2] <- [2][3]
			InverseMatrix.M[3][3] = Adj.M[3][3] * InvDet;
		}

		return InverseMatrix;
	}

	// 비교 연산자
	bool operator==(const FMatrix& Other) const
	{
		for (uint8 i = 0; i < 4; ++i)
		{
			for (uint8 j = 0; j < 4; ++j)
			{
				if (std::fabs(M[i][j] - Other.M[i][j]) >= KINDA_SMALL_NUMBER)
					return false;
			}
		}
		return true;
	}

	bool operator!=(const FMatrix& Other) const
	{
		return !(*this == Other);
	}

	FVector TransformPosition(const FVector& V) const;
	FVector TransformVector(const FVector& V) const;
	
	// View/Proj (L H)
	static FMatrix LookAtLH(const FVector& Eye, const FVector& At, const FVector& Up);
	static FMatrix PerspectiveFovLH(float FovY, float Aspect, float Zn, float Zf);
	static FMatrix OrthoLH(float Width, float Height, float Zn, float Zf);
	static FMatrix OrthoLH_XForward(float Width, float Height, float Xn, float Xf);
	static FMatrix OrthoMatrix(const FAABB& AABB);
	static FMatrix OrthoMatrix(float R, float L, float T, float B, float F, float N);
	// Perspective Projection 역행렬 계산 (Left-Handed)
	// PerspectiveFovLH로 생성된 투영 행렬의 역행렬을 계산합니다
	static FMatrix InversePerspectiveFovLH(float FovY, float Aspect, float Zn, float Zf)
	{
		float YScale = 1.0f / std::tan(FovY * 0.5f);
		float XScale = YScale / Aspect;
		
		// 원본 투영 행렬의 주요 성분들
		float A = Zf / (Zf - Zn);
		float B = (-Zn * Zf) / (Zf - Zn);
		
		FMatrix invProj{};
		/*invProj.Rows[0] = _mm_set_ps(0.0f, 0.0f, 0.0f, 1.0f / XScale);
		invProj.Rows[1] = _mm_set_ps(0.0f, 0.0f, 1.0f / YScale, 0.0f);
		invProj.Rows[2] = _mm_set_ps(0.0f, 0.0f, 0.0f, 0.0f);
		invProj.Rows[3] = _mm_set_ps(1.0f / B, 1.0f, 0.0f, -A / B);*/

		invProj.Rows[0] = _mm_set_ps(0.0f, 0.0f, 0.0f, 1.0f / XScale);
		invProj.Rows[1] = _mm_set_ps(0.0f, 0.0f, 1.0f / YScale, 0.0f);
		invProj.Rows[2] = _mm_set_ps(1.0f / B, 0.0f, 0.0f, 0.0f);
		invProj.Rows[3] = _mm_set_ps(-A / B, 1.0f, 0.0f, 0.0f);
		
		return invProj;
	}
	
	// 이미 생성된 Perspective Projection 행렬의 역행렬을 계산합니다
	FMatrix InversePerspectiveProjection() const
	{
		// 투영 행렬의 구조:
		// [XScale,  0,      0,     0]
		// [0,       YScale, 0,     0]
		// [0,       0,      A,     1]
		// [0,       0,      B,     0]
		
		float XScale = M[0][0];
		float YScale = M[1][1];
		float A = M[2][2];
		float B = M[3][2];
		
		// 안전성 체크
		if (std::fabs(XScale) < KINDA_SMALL_NUMBER ||
		    std::fabs(YScale) < KINDA_SMALL_NUMBER ||
		    std::fabs(B) < KINDA_SMALL_NUMBER)
		{
			return FMatrix::Identity();
		}
		
		FMatrix invProj{};
		invProj.Rows[0] = _mm_set_ps(0.0f, 0.0f, 0.0f, 1.0f / XScale);
		invProj.Rows[1] = _mm_set_ps(0.0f, 0.0f, 1.0f / YScale, 0.0f);
		invProj.Rows[2] = _mm_set_ps(1.0f/B, 0.0f, 0.0f, 0.0f);
		invProj.Rows[3] = _mm_set_ps(-A / B, 1.0f, 0.0f, 0.0f);
		
		return invProj;
	}

	FMatrix InverseOrthographicProjection() const
	{
		// Orthographic projection matrix structure:
	// [2/W,   0,    0,   0]
	// [0,   2/H,    0,   0]
	// [0,     0, 1/DZ,   0]
	// [0, -Zn/DZ,   0,   1]
	// Inverse:
	// [W/2,   0,    0,   0]
	// [0,   H/2,    0,   0]
	// [0,     0,  DZ,   0]
	// [0, Zn,      0,   1]
		float W = 2.0f / (M[0][0] != 0.0f ? M[0][0] : 1e-6f);
		float H = 2.0f / (M[1][1] != 0.0f ? M[1][1] : 1e-6f);
		float DZ = 1.0f / (M[2][2] != 0.0f ? M[2][2] : 1e-6f);
		float Zn = -M[3][2] * DZ;

		FMatrix inv{};
		inv.Rows[0] = _mm_set_ps(0.0f, 0.0f, 0.0f, W * 0.5f);
		inv.Rows[1] = _mm_set_ps(0.0f, 0.0f, H * 0.5f, 0.0f);
		inv.Rows[2] = _mm_set_ps(0.0f, DZ, 0.0f, 0.0f);
		inv.Rows[3] = _mm_set_ps(1.0f, Zn, 0.0f, 0.0f);
		return inv;
	}

	// FMinimalViewInfo의 값을 기반으로 투영 행렬을 생성하는 공통 유틸리티 함수입니다.
	static FMatrix CreateProjectionMatrix(
		float FieldOfView,       // (Degrees)
		float AspectRatio,
		float ViewWidth,
		float ViewHeight,
		float NearClip,
		float FarClip,
		float ZoomFactor,
		enum class ECameraProjectionMode ProjectionMode);

	// 직렬화 연산자
	friend FArchive& operator<<(FArchive& Ar, FMatrix& Matrix)
	{
		for (int i = 0; i < 4; ++i)
		{
			for (int j = 0; j < 4; ++j)
			{
				Ar.Serialize(&Matrix.M[i][j], sizeof(float));
			}
		}
		return Ar;
	}
};

// ─────────────────────────────
// 전역 연산자들
// ─────────────────────────────

// FVector4 * FMatrix (row-vector: v' = v * M)
inline FVector4 operator*(const FVector4& V, const FMatrix& M)
{
	// Splat V components into separate registers
	__m128 vX = _mm_shuffle_ps(V.SimdData, V.SimdData, _MM_SHUFFLE(0, 0, 0, 0));
	__m128 vY = _mm_shuffle_ps(V.SimdData, V.SimdData, _MM_SHUFFLE(1, 1, 1, 1));
	__m128 vZ = _mm_shuffle_ps(V.SimdData, V.SimdData, _MM_SHUFFLE(2, 2, 2, 2));
	__m128 vW = _mm_shuffle_ps(V.SimdData, V.SimdData, _MM_SHUFFLE(3, 3, 3, 3));

	// Multiply each component with the corresponding matrix row
	__m128 result = _mm_mul_ps(vX, M.Rows[0]);
	result = _mm_add_ps(result, _mm_mul_ps(vY, M.Rows[1]));
	result = _mm_add_ps(result, _mm_mul_ps(vZ, M.Rows[2]));
	result = _mm_add_ps(result, _mm_mul_ps(vW, M.Rows[3]));

	return FVector4(result);
}

// P를 점으로 계산
inline FVector operator*(const FVector& P, const FMatrix& M)
{
	// 1. FVector(x, y, z)를 동차 좌표계의 점 FVector4(x, y, z, 1.0)으로 확장합니다.
	//    FromPoint 헬퍼 함수가 이 역할을 수행합니다.
	const FVector4 Result4D = FVector4::FromPoint(P) * M;

	// 2. 원근 나눗셈(Perspective Divide)을 수행합니다.
	//    동차 좌표를 다시 3D 데카르트 좌표로 변환하기 위해 w로 나눕니다.
	const float W = Result4D.W;

	// w가 0에 매우 가까우면 나눗셈을 수행할 수 없으므로 안정성을 위해 확인합니다.
	// 이런 경우는 보통 점이 투영 평면(카메라의 z=0)에 정확히 위치할 때 발생합니다.
	if (std::fabs(W) > KINDA_SMALL_NUMBER)
	{
		// 나눗셈 대신 역수를 곱하는 것이 일반적으로 약간 더 빠릅니다.
		const float InvW = 1.0f / W;
		return FVector(
			Result4D.X * InvW,
			Result4D.Y * InvW,
			Result4D.Z * InvW
		);
	}

	// w가 0에 가까우면, 점이 무한대에 위치한 것으로 간주합니다.
	// 나눗셈을 생략하고 결과를 반환하여 NaN/INF 값 생성을 방지합니다.
	return FVector(Result4D.X, Result4D.Y, Result4D.Z);
}

// SIMD-accelerated matrix multiplication
inline FMatrix operator*(const FMatrix& A, const FMatrix& B)
{
	FMatrix Result;
	//const FMatrix& A = *this;

	// Transpose B to make columns into rows for easier processing
	//FMatrix B_T = B.Transpose();

	//for (int i = 0; i < 4; ++i)
	//{
	//    __m128 row = A.Rows[i];
	//    __m128 r0 = _mm_mul_ps(row, B_T.Rows[0]);
	//    __m128 r1 = _mm_mul_ps(row, B_T.Rows[1]);
	//    __m128 r2 = _mm_mul_ps(row, B_T.Rows[2]);
	//    __m128 r3 = _mm_mul_ps(row, B_T.Rows[3]);

	//    // Sum horizontally
	//    // r0 = (r0.x + r0.y + r0.z + r0.w, ...)
	//    __m128 sum_01 = _mm_hadd_ps(r0, r1);
	//    __m128 sum_23 = _mm_hadd_ps(r2, r3);
	//    Result.Rows[i] = _mm_hadd_ps(sum_01, sum_23);
	//}

	Result.VRows[0] = A.VRows[0] * B;
	Result.VRows[1] = A.VRows[1] * B;
	Result.VRows[2] = A.VRows[2] * B;
	Result.VRows[3] = A.VRows[3] * B;

	return Result;
}

// ─────────────────────────────
// FTransform (position/rotation/scale)
// ─────────────────────────────
struct FTransform
{
	FVector Translation;
	FQuat   Rotation;
	FVector Scale3D;

	FTransform() : Rotation(0, 0, 0, 1), Translation(0, 0, 0), Scale3D(1, 1, 1) {}
	FTransform(const FVector& T, const FQuat& R, const FVector& S) : Rotation(R), Translation(T), Scale3D(S) {}
	FTransform(const FMatrix& M);

	FMatrix ToMatrix() const;

	// Child 로컬 좌표계의 점을 부모 좌표계 변환과 합성. 호출하는 Transform의 결과 좌표계로 변환
	FTransform GetWorldTransform(const FTransform& ChildTransform) const
	{
		FTransform Result;

		// 회전 결합
		// Child 회전 후 부모 회전해야 로컬회전하므로 자식 먼저 곱해져야함
		Result.Rotation = Rotation * ChildTransform.Rotation;
		Result.Rotation.Normalize();

		// 스케일 결합 (component-wise)
		Result.Scale3D = FVector(
			Scale3D.X * ChildTransform.Scale3D.X,
			Scale3D.Y * ChildTransform.Scale3D.Y,
			Scale3D.Z * ChildTransform.Scale3D.Z
		);

		//
		// 부모 로컬 To World -> SRT, 자식 로컬 To 부모 -> Other.SRT
		// 자식 로컬 To World -> Other.SRT * SRT 
		// 자식 로컬 To World Translation -> Other.T * SRT = Translation(Rotation(Scale(Other.T)))
		FVector Scaled(ChildTransform.Translation.X * Scale3D.X,
			ChildTransform.Translation.Y * Scale3D.Y,
			ChildTransform.Translation.Z * Scale3D.Z);
		FVector Rotated = Rotation.RotateVector(Scaled);
		Result.Translation = Translation + Rotated;

		return Result;
	}
	//부모 로컬 좌표계로 자식 월드 Transform을 변환.
	//
	FTransform GetRelativeTransform(const FTransform& ChildTransform) const
	{
		const FTransform& Inverse = this->Inverse();

		FTransform Result;

		Result.Rotation = Inverse.Rotation * ChildTransform.Rotation;
		Result.Rotation.Normalize();

		Result.Scale3D = FVector(
			Inverse.Scale3D.X * ChildTransform.Scale3D.X,
			Inverse.Scale3D.Y * ChildTransform.Scale3D.Y,
			Inverse.Scale3D.Z * ChildTransform.Scale3D.Z
		);

		//(자식 To 부모 T) * (부모 To World SRT) = (자식 To World T)
		//(자식 To 부모 T) = InvScale(InvRotation(InvTranslation((자식 To World T))))
		//(자식 To 부모 T) = InvScale(InvRotation((ChildTransform.T - this->T) ))
		//주의할 점 : this->T랑 Inverse.T는 다름. Inverse.T는 역행렬의 Translation임.
		FVector ResultT = ChildTransform.Translation - this->Translation;
		ResultT = Inverse.Rotation.RotateVector(ResultT);
		Result.Translation = FVector(
			ResultT.X * Inverse.Scale3D.X, 
			ResultT.Y * Inverse.Scale3D.Y, 
			ResultT.Z * Inverse.Scale3D.Z );

		return Result;
	}

	// 역변환
	FTransform Inverse() const;

	// 유틸
	FVector TransformPosition(const FVector& P) const
	{
		// (R*S)*P + T
		FVector SP = FVector(P.X * Scale3D.X, P.Y * Scale3D.Y, P.Z * Scale3D.Z);
		FVector RP = Rotation.RotateVector(SP);
		return Translation + RP;
	}
	FVector TransformVector(const FVector& V) const
	{
		// R*(S*V) (translation 없음)
		FVector SV = FVector(V.X * Scale3D.X, V.Y * Scale3D.Y, V.Z * Scale3D.Z);
		return Rotation.RotateVector(SV);
	}

	static FTransform Lerp(const FTransform& A, const FTransform& B, float T)
	{
		FVector  TPosition = FVector::Lerp(A.Translation, B.Translation, T);
		FVector  TScale = FVector::Lerp(A.Scale3D, B.Scale3D, T);
		FQuat    TRotation = FQuat::Slerp(A.Rotation, B.Rotation, T);
		return FTransform(TPosition, TRotation, TScale);
	}

	/**
	 * @brief 두 Transform을 보간하여 현재 Transform에 저장
	 * @param A 첫 번째 Transform
	 * @param B 두 번째 Transform
	 * @param Alpha 보간 비율 (0.0 = A, 1.0 = B)
	 */
	void Blend(const FTransform& A, const FTransform& B, float Alpha)
	{
		Translation = FVector::Lerp(A.Translation, B.Translation, Alpha);
		Scale3D = FVector::Lerp(A.Scale3D, B.Scale3D, Alpha);
		Rotation = FQuat::Slerp(A.Rotation, B.Rotation, Alpha);
	}

	// 비교 연산자
	bool operator==(const FTransform& Other) const
	{
		return Translation == Other.Translation &&
			Rotation == Other.Rotation &&
			Scale3D == Other.Scale3D;
	}
	bool operator!=(const FTransform& Other) const { return !(*this == Other); }
};

// ─────────────────────────────
// Inline 구현부
// ─────────────────────────────

// v' = v + 2 * cross(q.xyz, cross(q.xyz, v) + q.w * v)
inline FVector FQuat::RotateVector(const FVector& V) const
{
	float n = X * X + Y * Y + Z * Z + W * W;
	if (n <= KINDA_SMALL_NUMBER) return V;

	const FVector U(X, Y, Z);
	const FVector T(
		2.0f * (U.Y * V.Z - U.Z * V.Y),
		2.0f * (U.Z * V.X - U.X * V.Z),
		2.0f * (U.X * V.Y - U.Y * V.X)
	);
	return FVector(
		V.X + W * T.X + (U.Y * T.Z - U.Z * T.Y),
		V.Y + W * T.Y + (U.Z * T.X - U.X * T.Z),
		V.Z + W * T.Z + (U.X * T.Y - U.Y * T.X)
	);
}

// FQuat → Matrix
inline FMatrix FQuat::ToMatrix() const
{
	float XX = X * X, YY = Y * Y, ZZ = Z * Z;
	float XY = X * Y, XZ = X * Z, YZ = Y * Z;
	float WX = W * X, WY = W * Y, WZ = W * Z;

	return FMatrix(
		1.0f - 2.0f * (YY + ZZ), 2.0f * (XY - WZ), 2.0f * (XZ + WY), 0.0f,
		2.0f * (XY + WZ), 1.0f - 2.0f * (XX + ZZ), 2.0f * (YZ - WX), 0.0f,
		2.0f * (XZ - WY), 2.0f * (YZ + WX), 1.0f - 2.0f * (XX + YY), 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	).Transpose();
}

// Row-major + 행벡터(p' = p * M), Left-Handed: X=Forward, Y=Right, Z=Up
inline FMatrix FMatrix::LookAtLH(const FVector& Eye, const FVector& At, const FVector& Up)
{
	// 엔진 좌표계로 기저 벡터 생성: X=Forward, Y=Right, Z=Up
	FVector ForwardV = (At - Eye).GetNormalized();  // Forward
	FVector RightV = FVector::Cross(Up, ForwardV).GetNormalized();  // Right
	FVector UpV = FVector::Cross(ForwardV, RightV);  // Up

	// Row-major + 행벡터: 뷰 행렬의 회전 부분은 카메라 기저의 전치
	FMatrix View;
	View.Rows[0] = _mm_set_ps(0.0f, UpV.X, RightV.X, ForwardV.X);
	View.Rows[1] = _mm_set_ps(0.0f, UpV.Y, RightV.Y, ForwardV.Y);
	View.Rows[2] = _mm_set_ps(0.0f, UpV.Z, RightV.Z, ForwardV.Z);
	View.Rows[3] = _mm_set_ps(1.0f, -FVector::Dot(Eye, UpV), -FVector::Dot(Eye, RightV), -FVector::Dot(Eye, ForwardV));

	// 엔진(X=forward, Y=right, Z=up) → 쉐이더(X=right, Y=up, Z=forward)
	static const FMatrix ToShaderCoords(
		0, 0, 1, 0,  
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 0, 1
	);

	return View * ToShaderCoords; // 이미 row-major로 구축함
}

inline FMatrix FMatrix::PerspectiveFovLH(float FovY, float Aspect, float Zn, float Zf)
{
	float YScale = 1.0f / std::tan(FovY * 0.5f);
	float XScale = YScale / Aspect;

	FMatrix proj{};
	proj.Rows[0] = _mm_set_ps(0.0f, 0.0f, 0.0f, XScale);
	proj.Rows[1] = _mm_set_ps(0.0f, 0.0f, YScale, 0.0f);
	proj.Rows[2] = _mm_set_ps(1.0f, Zf / (Zf - Zn), 0.0f, 0.0f); // ?? 왜 마지막에 1.0f ??
	proj.Rows[3] = _mm_set_ps(0.0f, (-Zn * Zf) / (Zf - Zn), 0.0f, 0.0f);
	return proj;
}

inline FMatrix FMatrix::OrthoLH(float Width, float Height, float Zn, float Zf)
{
	const float W = (Width != 0.0f) ? Width : 1e-6f;
	const float H = (Height != 0.0f) ? Height : 1e-6f;
	const float DZ = (Zf - Zn != 0.0f) ? (Zf - Zn) : 1e-6f;

	FMatrix m = FMatrix::Identity();
	m.Rows[0] = _mm_set_ps(0.0f, 0.0f, 0.0f, 2.0f / W);
	m.Rows[1] = _mm_set_ps(0.0f, 0.0f, 2.0f / H, 0.0f);
	m.Rows[2] = _mm_set_ps(0.0f, 1.0f / DZ, 0.0f, 0.0f);
	m.Rows[3] = _mm_set_ps(1.0f, -Zn / DZ, 0.0f, 0.0f);
	return m;
}

inline FMatrix FMatrix::OrthoLH_XForward(float Width, float Height, float Xn, float Xf)
{
	const float W = (Width != 0.0f) ? Width : 1e-6f;
	const float H = (Height != 0.0f) ? Height : 1e-6f;
	const float DX = (Xf - Xn != 0.0f) ? (Xf - Xn) : 1e-6f;

	FMatrix m = FMatrix::Identity();

	m.Rows[0] = _mm_set_ps(0.0f, 0.0f, 0.0f, 1.0f / DX);
	m.Rows[1] = _mm_set_ps(0.0f, 0.0f, 2.0f / W, 0.0f);
	m.Rows[2] = _mm_set_ps(0.0f, 2.0f / H, 0.0f, 0.0f);
	m.Rows[3] = _mm_set_ps(1.0f, 0.0f, 0.0f, -Xn / DX);
	return m;
}

// FTransform → Matrix (row-vector convention; translation in last row)
// inline FMatrix FTransform::ToMatrixWithScale() const
// {
//     float x2 = Rotation.X + Rotation.X;
//     float y2 = Rotation.Y + Rotation.Y;
//     float z2 = Rotation.Z + Rotation.Z;
//
//     float xx = Rotation.X * x2; float yy = Rotation.Y * y2; float zz = Rotation.Z * z2;
//     float xy = Rotation.X * y2; float xz = Rotation.X * z2; float yz = Rotation.Y * z2;
//     float wx = Rotation.W * x2; float wy = Rotation.W * y2; float wz = Rotation.W * z2;
//
//     float sx = Scale3D.X;
//     float sy = Scale3D.Y;
//     float sz = Scale3D.Z;
//
//     // Rows store basis vectors scaled by S; translation in last row
//     return FMatrix(
//         (1.0f - (yy + zz)) * sx, (xy + wz) * sx, (xz - wy) * sx, 0.0f,
//         (xy - wz) * sy, (1.0f - (xx + zz)) * sy, (yz + wx) * sy, 0.0f,
//         (xz + wy) * sz, (yz - wx) * sz, (1.0f - (xx + yy)) * sz, 0.0f,
//         Translation.X, Translation.Y, Translation.Z, 1.0f
//     );
// }

// FTransform을 FMatrix로 변환 S * R * T (row-major + 행벡터 규약)
inline FMatrix FTransform::ToMatrix() const
{
	FMatrix R = Rotation.ToMatrix();

	// Scale the rotation part using SIMD
	R.Rows[0] = _mm_mul_ps(R.Rows[0], _mm_set1_ps(Scale3D.X));
	R.Rows[1] = _mm_mul_ps(R.Rows[1], _mm_set1_ps(Scale3D.Y));
	R.Rows[2] = _mm_mul_ps(R.Rows[2], _mm_set1_ps(Scale3D.Z));

	// Set the translation part using SIMD
	R.Rows[3] = _mm_set_ps(1.0f, Translation.Z, Translation.Y, Translation.X);

	// The multiplication will use the SIMD-optimized operator*
	return R;
}

// FTransform 역변환
inline FTransform FTransform::Inverse() const
{
	// InvScale
	FVector InvScale(
		(std::fabs(Scale3D.X) > KINDA_SMALL_NUMBER) ? 1.0f / Scale3D.X : 0.0f,
		(std::fabs(Scale3D.Y) > KINDA_SMALL_NUMBER) ? 1.0f / Scale3D.Y : 0.0f,
		(std::fabs(Scale3D.Z) > KINDA_SMALL_NUMBER) ? 1.0f / Scale3D.Z : 0.0f
	);

	// InvRot = conjugate (단위 가정)
	FQuat InvRot(-Rotation.X, -Rotation.Y, -Rotation.Z, Rotation.W);

	//(SRT)^(-1) = T^(-1)R^(-1)S^(-1). Translation Factor : (0,0,0,1)*T^(-1)R^(-1)S^(-1)
	//InvTrans = -InvScale(InvRotation(Translation))
	FVector Rotated = InvRot.RotateVector(Translation);
	FVector Scaled(Translation.X * InvScale.X,
		Translation.Y * InvScale.Y,
		Translation.Z * InvScale.Z);
	FVector InvTrans(-Scaled);

	FTransform Out;
	Out.Rotation = InvRot;
	Out.Scale3D = InvScale;
	Out.Translation = InvTrans;
	return Out;
}

inline void operator*= (TArray<FVector>& Vectors, const FMatrix& Mat)
{
	uint32 VectorCount = Vectors.size();
	for (int i = 0; i < VectorCount; i++)
	{
		Vectors[i] = Vectors[i] * Mat;
	}
}
inline void operator*= (TArray<FVector4>& Vectors, const FMatrix& Mat)
{
	uint32 VectorCount = Vectors.size();
	for (int i = 0; i < VectorCount; i++)
	{
		Vectors[i] = Vectors[i] * Mat;
	}
}