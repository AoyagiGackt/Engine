// ガラス割れエフェクト ― ゲーム画面を Voronoi シャードで割り砕く
//
// フェーズ:
//   t 0.00-0.35 : ひびが impactUV から広がる（ゲーム画面の上にひびが入る）
//   t 0.35-1.00 : 各シャードが重力で落下してフェードアウト（ゲーム画面が崩れ落ちる）
//   t > 1.00    : 完全透明 → IsFinished() == true → クリア画面へ

cbuffer ShatterParams : register(b0)
{
    float time;        // 0→1 アニメーション進行
    float crackWidth;  // ひびの太さ（推奨 0.004〜0.008）
    float impactU;     // 衝撃点 U
    float impactV;     // 衝撃点 V
    float shardSpeed;  // シャードの落下量（UV 単位）
    float3 pad;
};

Texture2D<float4> sceneTex    : register(t0); // キャプチャしたゲーム画面
SamplerState      linearClamp : register(s0);

struct PSInput {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

// ---- ハッシュ関数 ----
float2 hash2(float2 p)
{
    float2 q = float2(dot(p, float2(127.1f, 311.7f)),
                      dot(p, float2(269.5f, 183.3f)));
    return frac(sin(q) * 43758.5453f);
}

float hash1(float2 p)
{
    return frac(sin(dot(p, float2(127.1f, 311.7f))) * 43758.5453f);
}

// ---- Voronoi: セルID (.xy) + エッジ距離 (.z) ----
float3 Voronoi(float2 uv)
{
    float2 i = floor(uv);
    float2 f = frac(uv);

    float  minDist = 10.0f;
    float2 minID   = float2(0.0f, 0.0f);
    float2 minVec  = float2(0.0f, 0.0f);

    [unroll]
    for (int ny = -1; ny <= 1; ny++) {
        [unroll]
        for (int nx = -1; nx <= 1; nx++) {
            float2 n = float2(nx, ny);
            float2 g = n + hash2(i + n) - f;
            float  d = dot(g, g);
            if (d < minDist) { minDist = d; minID = i + n; minVec = g; }
        }
    }

    float edge = 10.0f;
    [unroll]
    for (int ny2 = -2; ny2 <= 2; ny2++) {
        [unroll]
        for (int nx2 = -2; nx2 <= 2; nx2++) {
            float2 n = float2(nx2, ny2);
            float2 g = n + hash2(i + n) - f;
            float2 r = g - minVec;
            if (dot(r, r) > 0.0001f) {
                float2 mid = (g + minVec) * 0.5f;
                edge = min(edge, dot(mid, normalize(r)));
            }
        }
    }

    return float3(minID, edge);
}

float4 main(PSInput input) : SV_Target
{
    const float CELL_SCALE = 10.0f;

    float2 uv       = input.uv;
    float2 impactUV = float2(impactU, impactV);

    float2 toImpact      = uv - impactUV;
    float  distFromImpact = length(toImpact * float2(16.0f / 9.0f, 1.0f));

    // ---- フェーズ計算 ----
    float crackT = saturate(time / 0.35f);
    float flyT   = saturate((time - 0.35f) / 0.65f);

    // ひびの進行フロント
    float wavefront = crackT * 1.8f;
    float crackMask = saturate((wavefront - distFromImpact) / 0.35f);

    // ---- シャード落下オフセット ----
    // 粗いグリッドで所属を決める（Voronoi との循環依存を避けるため）
    float2 coarseCell = floor(uv * CELL_SCALE * 0.7f);
    float cr1 = hash1(coarseCell);
    float cr3 = hash1(coarseCell + float2(7.1f, 9.4f));

    float flyDelay = cr3 * 0.4f;
    float cellFlyT = saturate((flyT - flyDelay) / max(1.0f - flyDelay, 0.01f));

    // 重力落下（Y 正方向 = 下）+ 横方向の散らばり
    float2 drift = float2(
        (cr1 - 0.5f) * 0.25f * cellFlyT,
        cellFlyT * cellFlyT * max(shardSpeed, 0.1f)
    );

    // 逆マッピング: このピクセルに「落ちてきた」シャードは元々 sampleUV にあった
    float2 sampleUV = uv - drift;

    // シャードが画面外に落ちた → 透明
    if (sampleUV.x < 0.0f || sampleUV.x > 1.0f ||
        sampleUV.y < 0.0f || sampleUV.y > 1.0f)
        return float4(0.0f, 0.0f, 0.0f, 0.0f);

    // ---- ゲーム画面をサンプリング ----
    float4 scene = sceneTex.Sample(linearClamp, sampleUV);

    // ---- Voronoi でひびのエッジを計算（落下後の UV でサンプル → ひびもシャードと動く）----
    float3 voro = Voronoi(sampleUV * CELL_SCALE);
    float  edge = voro.z;

    // ---- ひびの描画 ----
    float crackLine = (1.0f - smoothstep(0.0f, crackWidth, edge)) * crackMask;
    float crackGlow = (1.0f - smoothstep(0.0f, crackWidth * 5.0f, edge)) * 0.25f * crackMask;
    float crackBright = saturate(crackLine + crackGlow);

    // ゲーム画面の上にひびラインを重ねる（白く光る）
    float3 col = lerp(scene.rgb, float3(1.0f, 1.0f, 1.0f), crackBright * 2.0f);

    // 落下とともにシャードをフェードアウト
    float cellAlpha = 1.0f - cellFlyT;

    return float4(col, cellAlpha);
}
