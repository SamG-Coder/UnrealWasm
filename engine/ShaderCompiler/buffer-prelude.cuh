// Small device ABI support library, not a lighting implementation.
// The compiler rejects shader resources outside the supported buffer profile.
typedef unsigned int uint;
template<typename T> __device__ T webcuda_load(const T* value) { return *value; }
template<typename T> struct StructuredBuffer {
    T* data;
    unsigned long long count;
    __device__ T& operator[](unsigned long long index) const { return data[index]; }
};
template<typename T> struct RWStructuredBuffer : StructuredBuffer<T> {};
__device__ uint U32_min(uint a, uint b) { return a < b ? a : b; }
__device__ uint2 make_uint2(uint a) { return make_uint2(a, a); }
__device__ uint2 operator*(uint2 a, uint2 b) { return make_uint2(a.x*b.x, a.y*b.y); }
