/**
 * @file MakeAffine.h
 * @brief 3Dゲームエンジンに必要な数学型・演算をまとめて取り込む集約ヘッダ
 * @note 実体はVector2/Vector3/Vector4/Quaternion/Matrix4x4/Transform/MathCollisionに分割されている。
 * 既存コードとの互換のため、このファイル1つをincludeすれば全て使えるようにしている。
 */
#pragma once
#include "MathCollision.h"
#include "Matrix4x4.h"
#include "Quaternion.h"
#include "Transform.h"
#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"
