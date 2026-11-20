#pragma once

#include <glm/gtx/compatibility.hpp>

#include "/Vector.hpp"
#include "Matrix/Matrix.hpp"
#include "Quaternion.hpp"

namespace SF::Engine
{
    using glm::abs;
    using glm::acos;
    using glm::acosh;
    using glm::all;
    using glm::any;
    using glm::asin;
    using glm::asinh;
    using glm::atan;
    using glm::atanh;
    using glm::bitCount;
    using glm::bitfieldExtract;
    using glm::bitfieldInsert;
    using glm::bitfieldReverse;
    using glm::ceil;
    using glm::clamp;
    using glm::cos;
    using glm::cosh;
    using glm::cross;
    using glm::degrees;
    using glm::determinant;
    using glm::distance;
    using glm::dot;
    using glm::equal;
    using glm::exp;
    using glm::exp2;
    using glm::faceforward;
    using glm::findLSB;
    using glm::findMSB;
    using glm::floatBitsToInt;
    using glm::floatBitsToUint;
    using glm::floor;
    using glm::fma;
    using glm::fract;
    using glm::frexp;
    using glm::greaterThan;
    using glm::greaterThanEqual;
    using glm::imulExtended;
    using glm::intBitsToFloat;
    using glm::inverse;
    using glm::inversesqrt;
    using glm::isinf;
    using glm::isnan;
    using glm::ldexp;
    using glm::length;
    using glm::lessThan;
    using glm::lessThanEqual;
    using glm::log;
    using glm::log2;
    using glm::matrixCompMult;
    using glm::max;
    using glm::min;
    using glm::mix;
    using glm::mod;
    using glm::modf;
    using glm::normalize;
    using glm::not_;
    using glm::notEqual;
    using glm::outerProduct;
    using glm::packDouble2x32;
    using glm::packHalf2x16;
    using glm::packSnorm2x16;
    using glm::packSnorm4x8;
    using glm::packUnorm2x16;
    using glm::packUnorm4x8;
    using glm::pow;
    using glm::radians;
    using glm::reflect;
    using glm::refract;
    using glm::round;
    using glm::roundEven;
    using glm::sign;
    using glm::sin;
    using glm::sinh;
    using glm::smoothstep;
    using glm::sqrt;
    using glm::step;
    using glm::tan;
    using glm::tanh;
    using glm::transpose;
    using glm::trunc;
    using glm::uaddCarry;
    using glm::uintBitsToFloat;
    using glm::umulExtended;
    using glm::unpackDouble2x32;
    using glm::unpackHalf2x16;
    using glm::unpackSnorm2x16;
    using glm::unpackSnorm4x8;
    using glm::unpackUnorm2x16;
    using glm::unpackUnorm4x8;
    using glm::usubBorrow;

    using glm::double2;
    using glm::double2x2;
    using glm::double2x3;
    using glm::double2x4;
    using glm::double3;
    using glm::double3x2;
    using glm::double3x3;
    using glm::double3x4;
    using glm::double4;
    using glm::double4x2;
    using glm::double4x3;
    using glm::double4x4;

    using glm::int2;
    using glm::int2x2;
    using glm::int2x3;
    using glm::int2x4;
    using glm::int3;
    using glm::int3x2;
    using glm::int3x3;
    using glm::int3x4;
    using glm::int4;
    using glm::int4x2;
    using glm::int4x3;
    using glm::int4x4;

    using glm::float2;
    using glm::float2x2;
    using glm::float2x3;
    using glm::float2x4;
    using glm::float3;
    using glm::float3x2;
    using glm::float3x3;
    using glm::float3x4;
    using glm::float4;
    using glm::float4x2;
    using glm::float4x3;
    using glm::float4x4;

    using glm::bool2;
    using glm::bool2x2;
    using glm::bool2x3;
    using glm::bool2x4;
    using glm::bool3;
    using glm::bool3x2;
    using glm::bool3x3;
    using glm::bool3x4;
    using glm::bool4;
    using glm::bool4x2;
    using glm::bool4x3;
    using glm::bool4x4;

    using LongDouble2   = glm::vec<2, long double, glm::qualifier::highp>;
    using LongDouble3   = glm::vec<3, long double, glm::qualifier::highp>;
    using LongDouble4   = glm::vec<4, long double, glm::qualifier::highp>;
    using LongDouble2x2 = glm::mat<2, 2, long double, glm::qualifier::highp>;
    using LongDouble2x3 = glm::mat<2, 3, long double, glm::qualifier::highp>;
    using LongDouble2x4 = glm::mat<2, 4, long double, glm::qualifier::highp>;
    using LongDouble3x2 = glm::mat<3, 2, long double, glm::qualifier::highp>;
    using LongDouble3x3 = glm::mat<3, 3, long double, glm::qualifier::highp>;
    using LongDouble3x4 = glm::mat<3, 4, long double, glm::qualifier::highp>;
    using LongDouble4x2 = glm::mat<4, 2, long double, glm::qualifier::highp>;
    using LongDouble4x3 = glm::mat<4, 3, long double, glm::qualifier::highp>;
    using LongDouble4x4 = glm::mat<4, 4, long double, glm::qualifier::highp>;
} // namespace SF::Engine
