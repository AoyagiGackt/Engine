// ガラス割れエフェクト ― ゲーム画面をガラスの破片に割り砕く
// （斬線が1本以上あれば斬線で区切った破片、無ければ割れ始めの点から放射状に広がるガラスの破片で割る）
//
// reassemble = 0（大技の世界割れなど）:
//   t 0.00-0.35 : ひびが割れ始めの点から放射状に広がり、破片のすき間が少し開く
//   t 0.35-1.00 : 割れ始めの点に近い破片から順に、光りながら落ちてフェードアウトする（後ろの画面が見える）
// reassemble = 1（クリア演出）:
//   t 0.00-0.45 : 上と同じ割れ方を縮めて再生し、破片の抜けた所は暗い空間にする
//   t 0.50-1.00 : 割れて落ちた動きを巻き戻し、割れた時と同じ形の破片がガラスになって戻る。はまった所から後ろの画面（結果画面）が見える

cbuffer ShatterParams : register(b0)
{
    float time;        // 0→1 アニメーション進行
    float crackWidth;  // ひびの太さ（推奨 0.004〜0.008）
    float impactU;     // 衝撃点 U
    float impactV;     // 衝撃点 V
    float shardSpeed;  // シャードの落下量（UV 単位）
    uint cutLineCount;
    float cutSeparateEnd;
    float cutSlide;
    float cutGlowWidth;
    float cutGlowStrength;
    float cutFallDelay;
    float cutSpread;
    float4 cutLines[32];
    uint impactCount;   // 0 なら impactU/V の1点だけ
    uint reassemble;    // 1 なら割れた後に破片が集まり直して後ろの画面になる
    float2 padding;
    float4 impacts[16]; // 銃の弾痕など、割れ始める点（xy）
    uint cutPieceCount;           // 中心を求めた斬線の破片の数
    uint3 piecePadding;
    uint4 cutPieceMasks[64];      // 破片の識別（斬線の左右の組み合わせ）を4つずつ
    float4 cutPieceCenters[256];  // 破片の中心（xy、UV）と半径（z、画面の高さ単位）
};

Texture2D<float4> sceneTex    : register(t0); // キャプチャしたゲーム画面
SamplerState      linearClamp : register(s0);

// 全画面描画から、画面座標とテクスチャ座標を受け取る。
struct PSInput
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

// 乱数とセル分割の共通値。
static const float2 kHashAxisX = float2(127.1f, 311.7f);
static const float2 kHashAxisY = float2(269.5f, 183.3f);
static const float kHashScale = 43758.5453f;
static const float kVoronoiInitialDistance = 10.0f;
static const int kVoronoiEdgeRadius = 2;
static const float kVoronoiMinDistanceSquared = 0.0001f;
static const float kScreenAspect = 16.0f / 9.0f;
static const float kTwoPi = 6.2831853f;
static const float kHalf = 0.5f;
static const float kMinInterval = 0.00001f;

// 放射状のガラスの破片（割れ始めの点を中心にした角度と距離の対数でセルを作る。中心ほど細かく、外ほど大きい）
static const float kGlassAngularCells = 13.0f;   // 1周を何個に分けるか
static const float kGlassRadialCells = 1.7f;     // 距離の対数1あたりの段数（小さいほど放射方向に細長い）
static const float kGlassLogOffset = 0.03f;      // 中心の極端に細かいセルを抑える
static const float kImpactSeedStride = 37.0f;    // 割れ始めの点ごとにセルの模様を変える
static const float kGlassCrackWidth = 0.035f;    // ひびの太さ（セル空間の単位）
static const float kGlassCrackGlowWidth = 0.12f; // ひびのにじみの太さ
static const float kGlassCrackGlow = 0.3f;
static const float kCrackBrightness = 2.0f;
static const uint kMaxImpacts = 16;
static const uint kInverseIterations = 3;

// 割れて落ちる時の動き
static const float kSeparateEnd = 0.35f;      // ひびが広がり切る時刻
static const float kWaveRadius = 1.8f;        // ひびが広がり切る距離（画面の高さ単位）
static const float kSeparatePush = 0.006f;    // ひびが入った後、破片が中心から離れて開くすき間
static const float kImpactOrderWeight = 0.75f; // 割れ始めの点に近い順に落とす割合（残りは破片ごとのばらつき）
static const float kImpactReach = 1.4f;       // この距離より遠い破片は一番最後に落ちる
static const float kImpactMaxDelay = 0.6f;
static const float kShardSpread = 0.25f;      // 落ちる時の横の散らばり
static const float kMinFallSpeed = 0.1f;
static const float2 kDelaySeed = float2(7.1f, 9.4f);
static const float2 kSpreadSeed2 = float2(3.7f, 1.3f);

// 落ちる破片のきらめきと濃淡
static const float kGlintSpeed = 9.0f;
static const float kGlintSharpness = 12.0f;
static const float kGlintStrength = 0.6f;
static const float kShardShade = 0.15f;
static const float kShadeRampSpeed = 4.0f;

// 集まり直す時の動き（reassemble = 1）
static const float kBreakEnd = 0.45f;          // ここまでで割れて散る
static const float kAssembleStart = 0.5f;      // ここから破片が戻り始める
static const float kRewindEnd = 0.8f;          // 集まる段階のここまでで割れた動きを巻き戻し終える（残りは光とひびが消えるのを見せる）
static const float kLandFlashSpan = 0.07f;     // はまった瞬間に白く光る長さ（割れの時刻の単位で、巻き戻した量）
static const float kLandFlashStrength = 0.9f;
static const float kCrackFadeStart = 0.85f;    // はまった破片のひびが消え始める
static const float kAssembledCrack = 0.8f;
static const float3 kVoidColor = float3(0.02f, 0.025f, 0.06f); // 破片が抜けた後の暗い空間
static const float3 kGlassColor = float3(0.72f, 0.88f, 1.0f);  // 飛んでいる間のガラスの色
static const float kGlassAlpha = 0.6f;
static const float kGlassRimWidth = 0.15f;     // 飛んでいるガラスの縁を明るくする幅（セル空間の単位）
static const float kGlassRimStrength = 0.8f;

// 斬線の破片が集まり直す時の縁とひび（画面の高さ単位）
static const float kCutRimWidth = 0.012f;       // 飛んでいる破片の縁を明るくする幅
static const float kCutLandCrackWidth = 0.003f; // はまった破片の切れ目の太さ

// 集まり直す時に破片ごとに回す
static const float kReassembleSpin = 6.5f;      // 落ち切った所での回転の最大（ラジアン、約1周）。はまるにつれて0へ減る
static const uint kReassembleIterations = 6;    // 回っている放射状の破片を探す反復の回数
static const int kNeighborRadius = 1;           // 反復で見つからない時に確かめる周りのセルの範囲
static const uint kMaxCutPieces = 256;         // 中心を送ってくる斬線の破片の上限（C++ 側と同じ）
static const uint kMasksPerVector = 4u;
static const float2 kSpinSeed = float2(8.3f, 4.7f);
static const uint kCutSpinSeed = 0x165667b1u;
static const float kFallNotFound = 2.0f;        // まだ破片が見つかっていない印（落ちる量は0〜1）

// ---- 乱数 ----
// セルごとの二次元乱数を作る。
float2 hash2(float2 p)
{
    float2 q = float2(dot(p, kHashAxisX), dot(p, kHashAxisY));
    return frac(sin(q) * kHashScale);
}

// セルごとの一次元乱数を作る。
float hash1(float2 p)
{
    return frac(sin(dot(p, kHashAxisX)) * kHashScale);
}

// ---- Voronoi ----
// 一番近いセルの番号だけを求める（破片の所属の判定用）。
float2 VoronoiCell(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float minDist = kVoronoiInitialDistance;
    float2 minID = i;
    [unroll]
    for (int ny = -1; ny <= 1; ny++)
    {
        [unroll]
        for (int nx = -1; nx <= 1; nx++)
        {
            float2 n = float2(nx, ny);
            float2 g = n + hash2(i + n) - f;
            float d = dot(g, g);
            if (d < minDist)
            {
                minDist = d;
                minID = i + n;
            }
        }
    }
    return minID;
}

// 一番近いセルの境界までの距離を求める（ひびの描画用）。
float VoronoiEdge(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float minDist = kVoronoiInitialDistance;
    float2 minVec = float2(0.0f, 0.0f);
    [unroll]
    for (int ny = -1; ny <= 1; ny++)
    {
        [unroll]
        for (int nx = -1; nx <= 1; nx++)
        {
            float2 n = float2(nx, ny);
            float2 g = n + hash2(i + n) - f;
            float d = dot(g, g);
            if (d < minDist)
            {
                minDist = d;
                minVec = g;
            }
        }
    }
    float edge = kVoronoiInitialDistance;
    [unroll]
    for (int ny2 = -kVoronoiEdgeRadius; ny2 <= kVoronoiEdgeRadius; ny2++)
    {
        [unroll]
        for (int nx2 = -kVoronoiEdgeRadius; nx2 <= kVoronoiEdgeRadius; nx2++)
        {
            float2 n = float2(nx2, ny2);
            float2 g = n + hash2(i + n) - f;
            float2 r = g - minVec;
            if (dot(r, r) > kVoronoiMinDistanceSquared)
            {
                edge = min(edge, dot((g + minVec) * kHalf, normalize(r)));
            }
        }
    }
    return edge;
}

// ---- 割れ始めの点 ----
// index 番目の割れ始めの点（点が無ければ impactU/V）。
float2 ImpactAt(uint index)
{
    return impactCount == 0u ? float2(impactU, impactV) : impacts[index].xy;
}

// 一番近い割れ始めの点の番号。
uint NearestImpact(float2 uv)
{
    uint count = impactCount == 0u ? 1u : min(impactCount, kMaxImpacts);
    float2 aspect = float2(kScreenAspect, 1.0f);
    uint nearest = 0u;
    float nearestDistance = kVoronoiInitialDistance;
    [loop]
    for (uint index = 0u; index < count; ++index)
    {
        float d = length((uv - ImpactAt(index)) * aspect);
        if (d < nearestDistance)
        {
            nearestDistance = d;
            nearest = index;
        }
    }
    return nearest;
}

// 一番近い割れ始めの点までの距離（画面の高さ単位）。
float DistanceToImpact(float2 uv)
{
    return length((uv - ImpactAt(NearestImpact(uv))) * float2(kScreenAspect, 1.0f));
}

// ---- 放射状のガラスの破片 ----
// 画面の点を、割れ始めの点を中心にした（角度, 距離の対数）のセル空間へ移す。
float2 GlassSpace(float2 uv, uint impactIndex)
{
    float2 d = (uv - ImpactAt(impactIndex)) * float2(kScreenAspect, 1.0f);
    float angle = atan2(d.y, d.x);
    return float2(angle / kTwoPi * kGlassAngularCells + float(impactIndex) * kImpactSeedStride,
                  log(length(d) + kGlassLogOffset) * kGlassRadialCells);
}

// 画面の点が属する破片（セル番号 xy と割れ始めの点の番号 z）。
float3 GlassCell(float2 uv)
{
    uint impactIndex = NearestImpact(uv);
    return float3(VoronoiCell(GlassSpace(uv, impactIndex)), float(impactIndex));
}

// 破片の、割れ始めの点から見た向き（画面の高さ単位の単位ベクトル）と距離。
float3 GlassCellPolar(float3 cell)
{
    float angle = (cell.x + kHalf - cell.z * kImpactSeedStride) / kGlassAngularCells * kTwoPi;
    float radius = exp((cell.y + kHalf) / kGlassRadialCells) - kGlassLogOffset;
    return float3(cos(angle), sin(angle), max(radius, 0.0f));
}

// 画面の点のひびの濃さ（0〜1）。
float GlassCrack(float2 uv)
{
    float edge = VoronoiEdge(GlassSpace(uv, NearestImpact(uv)));
    float crackLine = 1.0f - smoothstep(0.0f, kGlassCrackWidth, edge);
    float glow = (1.0f - smoothstep(0.0f, kGlassCrackGlowWidth, edge)) * kGlassCrackGlow;
    return saturate(crackLine + glow);
}

// 落ちていく破片がときどき光を返すきらめき。
float ShardGlint(float seed, float fall)
{
    float wave = saturate(sin(fall * kGlintSpeed + seed * kTwoPi));
    return pow(wave, kGlintSharpness) * kGlintStrength * saturate(fall * kShadeRampSpeed);
}

// 破片が落ち始めるまでの遅れ（落ちる段階に対する割合。割れ始めの点に近い破片ほど早い）。
float GlassFallDelay(float3 cell)
{
    float order = saturate(GlassCellPolar(cell).z / kImpactReach);
    return lerp(hash1(cell.xy + kDelaySeed), order, kImpactOrderWeight) * kImpactMaxDelay;
}

// 破片ごとの落ちる進み具合（割れ始めの点に近い破片から順に落ちる）。
float GlassFallProgress(float3 cell, float t)
{
    float flyT = saturate((t - kSeparateEnd) / (1.0f - kSeparateEnd));
    float delay = GlassFallDelay(cell);
    return saturate((flyT - delay) / max(1.0f - delay, kMinInterval));
}

// 破片ごとの移動量（ひびの後に中心から少し開き、その後は重力で落ちる）。
float2 GlassFallOffset(float3 cell, float t)
{
    float3 polar = GlassCellPolar(cell);
    float separate = smoothstep(0.0f, kSeparateEnd, t);
    float fall = GlassFallProgress(cell, t);
    float2 offset = polar.xy * kSeparatePush * separate;
    offset += float2((hash1(cell.xy + kSpreadSeed2) - kHalf) * kShardSpread * fall,
                     fall * fall * max(shardSpeed, kMinFallSpeed));
    return offset / float2(kScreenAspect, 1.0f);
}

// 時刻 t にこのピクセルへ来ている放射状の破片を、元の位置へ戻して探す（見つからなければ false）。
bool FindGlassShard(float2 uv, float t, out float3 cell, out float2 sampleUV)
{
    cell = GlassCell(uv);
    [unroll]
    for (uint iteration = 0u; iteration < kInverseIterations; ++iteration)
    {
        float3 source = GlassCell(uv - GlassFallOffset(cell, t));
        if (all(source == cell))
        {
            break;
        }
        cell = source;
    }
    sampleUV = uv - GlassFallOffset(cell, t);
    return all(GlassCell(sampleUV) == cell) && all(sampleUV >= 0.0f) && all(sampleUV <= 1.0f);
}

// 放射状のガラスで割って落とす（破片の無い所は透明）。
float4 DrawGlassFall(float2 uv, float t)
{
    float3 cell;
    float2 sampleUV;
    if (!FindGlassShard(uv, t, cell, sampleUV))
    {
        return 0.0f;
    }

    // ひびは割れ始めの点から広がる
    float crackT = saturate(t / kSeparateEnd);
    float crackMask = saturate((crackT * kWaveRadius - DistanceToImpact(sampleUV)) / kSeparateEnd);
    float crack = GlassCrack(sampleUV) * crackMask;

    float fall = GlassFallProgress(cell, t);
    float seed = hash1(cell.xy);
    float shade = 1.0f + (seed - kHalf) * kShardShade * saturate(fall * kShadeRampSpeed);
    float4 scene = sceneTex.SampleLevel(linearClamp, sampleUV, 0.0f);
    float3 color = lerp(scene.rgb * shade, 1.0f, saturate(crack * kCrackBrightness + ShardGlint(seed, fall)));
    return float4(color, 1.0f - fall);
}

// ---- 斬線で割る ----
// 斬線の判定と移動量には、画面の高さを単位とする座標を使う。
static const float kCutAspect = kScreenAspect;
static const uint kCutMaxLines = 32;
static const uint kHashShiftA = 16;
static const uint kHashShiftB = 15;
static const uint kHashMultiplierA = 0x7feb352du;
static const uint kHashMultiplierB = 0x846ca68bu;
static const uint kHashFractionMask = 0x00ffffffu;
static const float kHashFractionScale = 1.0f / 16777216.0f;
static const uint kSlideSeed = 0x9e3779b9u;
static const uint kFallSeed = 0x85ebca6bu;
static const uint kSpreadSeed = 0xc2b2ae35u;
static const float kSignedRange = 2.0f;

// ビットマスクの全ビットを混ぜ、破片ごとの乱数を作る。
float CutHash(uint mask)
{
    mask ^= mask >> kHashShiftA;
    mask *= kHashMultiplierA;
    mask ^= mask >> kHashShiftB;
    mask *= kHashMultiplierB;
    mask ^= mask >> kHashShiftA;
    return float(mask & kHashFractionMask) * kHashFractionScale;
}

// すべての斬線に対する左右の組み合わせで、破片を識別する。
uint CutMask(float2 uv)
{
    float2 pixel = uv * float2(kCutAspect, 1.0f);
    uint mask = 0u;
    [loop]
    for (uint index = 0u; index < min(cutLineCount, kCutMaxLines); ++index)
    {
        float4 cutLine = cutLines[index];
        float signedDistance = dot(pixel - cutLine.xy * float2(kCutAspect, 1.0f), cutLine.zw);
        if (signedDistance >= 0.0f)
        {
            mask |= 1u << index;
        }
    }
    return mask;
}

// 破片の順番（決めの一太刀が通る破片が0、間にある斬線が多い破片ほど1に近い）。
float CutImpactOrder(uint mask)
{
    uint impactMask = CutMask(float2(impactU, impactV));
    return saturate(float(countbits(mask ^ impactMask)) / max(float(min(cutLineCount, kCutMaxLines)) * kHalf, 1.0f));
}

// 画面の点から一番近い斬線までの距離（画面の高さ単位）。
float CutEdgeDistance(float2 uv)
{
    float edge = kCutAspect;
    [loop]
    for (uint index = 0u; index < min(cutLineCount, kCutMaxLines); ++index)
    {
        float4 cutLine = cutLines[index];
        edge = min(edge, abs(dot((uv - cutLine.xy) * float2(kCutAspect, 1.0f), cutLine.zw)));
    }
    return edge;
}

// 斬線の破片が落ち始めるまでの遅れ（落ちる段階に対する割合。決めの一太刀が通る破片から）。
float CutFallDelay(uint mask)
{
    return lerp(CutHash(mask ^ kFallSeed), CutImpactOrder(mask), kImpactOrderWeight) * cutFallDelay;
}

// 破片ごとの落下の進み具合（決めの一太刀が通る破片から、間にある斬線が多い破片ほど後に落ちる）。
float CutFallProgress(uint mask, float t)
{
    float flyTime = saturate((t - cutSeparateEnd) / max(1.0f - cutSeparateEnd, kMinInterval));
    float delay = CutFallDelay(mask);
    return saturate((flyTime - delay) / max(1.0f - delay, kMinInterval));
}

// 落ちる量 fall のときの、落下による移動（画面の高さ単位。横に散りながら下へ落ちる）。
float2 CutFallOffset(uint mask, float fall)
{
    float horizontal = (CutHash(mask ^ kSpreadSeed) * kSignedRange - 1.0f) * cutSpread;
    return float2(horizontal * fall, shardSpeed * fall * fall);
}

// 斬線に沿う滑りと、切断面を開く移動に落下を重ねる。
float2 CutDisplacement(uint mask, float t)
{
    float2 slide = 0.0f;
    float separateTime = saturate(t / max(cutSeparateEnd, kMinInterval));
    float slideVariation = kHalf + kHalf * CutHash(mask);
    [loop]
    for (uint index = 0u; index < min(cutLineCount, kCutMaxLines); ++index)
    {
        float2 normal = cutLines[index].zw;
        float2 tangent = float2(normal.y, -normal.x);
        float side = (mask & (1u << index)) != 0u ? 1.0f : -1.0f;
        float along = CutHash(mask ^ (kSlideSeed + index)) * kSignedRange - 1.0f;
        slide += normal * side + tangent * along;
    }
    // 線が多い場合も、スライドの最大量を一定に保つ。
    slide /= max(length(slide), 1.0f);
    slide *= cutSlide * slideVariation * separateTime;
    slide += CutFallOffset(mask, CutFallProgress(mask, t));
    return slide / float2(kCutAspect, 1.0f);
}

// 時刻 t にこのピクセルへ来ている斬線の破片を、元の位置へ戻して探す（見つからなければ false）。
bool FindCutShard(float2 uv, float t, out uint mask, out float2 sampleUV)
{
    mask = CutMask(uv);
    [unroll]
    for (uint iteration = 0u; iteration < kInverseIterations; ++iteration)
    {
        uint sourceMask = CutMask(uv - CutDisplacement(mask, t));
        if (sourceMask == mask)
        {
            break;
        }
        mask = sourceMask;
    }
    // 最終候補の移動量で逆写像し、画面外と未収束の候補を除く。
    sampleUV = uv - CutDisplacement(mask, t);
    return CutMask(sampleUV) == mask && all(sampleUV >= 0.0f) && all(sampleUV <= 1.0f);
}

// 戻した位置の所属が一致した破片だけを描き、切れ目を透明にする（割れ目は斬線だけなので、ひびも斬線の上にだけ光らせる）。
float4 DrawCutShards(float2 uv, float t)
{
    uint mask;
    float2 sampleUV;
    if (!FindCutShard(uv, t, mask, sampleUV))
    {
        return 0.0f;
    }

    // 元の切断面からの距離で白い光を付け、分離とともに弱める。
    float edge = CutEdgeDistance(sampleUV);
    float separate = saturate(t / max(cutSeparateEnd, kMinInterval));
    float glow = (1.0f - smoothstep(0.0f, max(cutGlowWidth, kMinInterval), edge)) * cutGlowStrength * (1.0f - separate);
    float fall = CutFallProgress(mask, t);
    float glint = ShardGlint(CutHash(mask ^ kSpreadSeed), fall);
    float4 scene = sceneTex.SampleLevel(linearClamp, sampleUV, 0.0f);
    return float4(lerp(scene.rgb, 1.0f, saturate(glow + glint)), scene.a * (1.0f - fall));
}

// 斬線があれば斬線で、無ければ放射状のガラスで割る。
float4 DrawBreak(float2 uv, float t)
{
    return cutLineCount > 0u ? DrawCutShards(uv, t) : DrawGlassFall(uv, t);
}

// ---- 集まり直す ----
// 割れて落ちた動きをそのまま巻き戻すので、戻ってくる破片は割れた時と同じ形になる。

// 巻き戻しで戻ってくる破片の見た目。飛んでいる間は縁の明るいガラスで、外にいるほど薄い。
// はまった後は白を重ねる濃さだけ返し、残りは後ろの画面（はまった瞬間に白く光り、ひびは終わりにかけて消える）。
// fall は落ちている量（0=はまった）、sinceLanding ははまってから巻き戻した量、rim と crack は縁・ひびの濃さ（0〜1）、t は集まる段階の時刻。
float4 ShadeReassembling(float fall, float sinceLanding, float rim, float crack, float glintSeed, float t)
{
    if (fall > 0.0f)
    {
        float shine = saturate(rim * kGlassRimStrength + ShardGlint(glintSeed, fall));
        float3 glass = lerp(kGlassColor, 1.0f, shine);
        float alpha = (kGlassAlpha + (1.0f - kGlassAlpha) * shine) * (1.0f - fall);
        return float4(lerp(kVoidColor, glass, alpha), 1.0f);
    }
    float flash = (1.0f - saturate(sinceLanding / kLandFlashSpan)) * kLandFlashStrength;
    float fade = crack * kAssembledCrack * (1.0f - smoothstep(kCrackFadeStart, 1.0f, t));
    return float4(1.0f, 1.0f, 1.0f, saturate(flash + fade));
}

// 戻ってくる途中の回転角（seed は破片ごとの乱数。遠くにいるほど大きく回っていて、はまる時に0になる）。
float ReassembleAngle(float seed, float fall)
{
    return (seed * kSignedRange - 1.0f) * kReassembleSpin * fall * fall;
}

// 画面の点 uv を、中心 pivot のまわりに angle 回して offset ずらした破片の、元の位置へ戻す
// （位置はどれも UV。形が縦横比でゆがまないよう、回すのは画面の高さ単位の座標で行う）。
float2 InverseRigid(float2 uv, float2 pivot, float angle, float2 offset)
{
    float2 aspect = float2(kScreenAspect, 1.0f);
    float2 turned = (uv - offset - pivot) * aspect;
    float c = cos(angle);
    float s = sin(angle);
    return pivot + float2(c * turned.x + s * turned.y, c * turned.y - s * turned.x) / aspect;
}

// 放射状の破片の中心（Voronoi の点の位置、UV）。
float2 GlassCellCenter(float3 cell)
{
    float2 site = cell.xy + hash2(cell.xy);
    float angle = (site.x - cell.z * kImpactSeedStride) / kGlassAngularCells * kTwoPi;
    float radius = max(exp(site.y / kGlassRadialCells) - kGlassLogOffset, 0.0f);
    return ImpactAt(uint(cell.z)) + float2(cos(angle), sin(angle)) * radius / float2(kScreenAspect, 1.0f);
}

// 放射状の破片 cell が回りながら戻ってくる途中で、画面の点 uv に見えている元の位置。
float2 GlassTurningHome(float2 uv, float3 cell, float breakT)
{
    float angle = ReassembleAngle(hash1(cell.xy + kSpinSeed), GlassFallProgress(cell, breakT));
    return InverseRigid(uv, GlassCellCenter(cell), angle, GlassFallOffset(cell, breakT));
}

// 元の位置 homeUV が、本当に破片 cell の中で画面の内側か。
bool IsGlassHome(float3 cell, float2 homeUV)
{
    return all(GlassCell(homeUV) == cell) && all(homeUV >= 0.0f) && all(homeUV <= 1.0f);
}

// 回りながら戻ってくる放射状の破片を探す。反復で見つからなければ、最後の候補のまわりのセルも確かめる。
bool FindGlassShardTurning(float2 uv, float breakT, out float3 cell, out float2 homeUV)
{
    cell = GlassCell(uv);
    [loop]
    for (uint iteration = 0u; iteration < kReassembleIterations; ++iteration)
    {
        homeUV = GlassTurningHome(uv, cell, breakT);
        float3 source = GlassCell(homeUV);
        if (all(source == cell))
        {
            return IsGlassHome(cell, homeUV);
        }
        cell = source;
    }
    float3 last = cell;
    [loop]
    for (int dy = -kNeighborRadius; dy <= kNeighborRadius; ++dy)
    {
        [loop]
        for (int dx = -kNeighborRadius; dx <= kNeighborRadius; ++dx)
        {
            cell = last + float3(dx, dy, 0.0f);
            homeUV = GlassTurningHome(uv, cell, breakT);
            if (IsGlassHome(cell, homeUV))
            {
                return true;
            }
        }
    }
    return false;
}

// index 番目の斬線の破片の識別と、中心（xy、UV）と半径（z、画面の高さ単位）。
uint CutPieceMask(uint index)
{
    return cutPieceMasks[index / kMasksPerVector][index % kMasksPerVector];
}

// 点 uv が識別 mask の斬線の破片に入っているか（合わない斬線が見つかった所で打ち切る）。
bool InCutShard(float2 uv, uint mask)
{
    float2 pixel = uv * float2(kCutAspect, 1.0f);
    [loop]
    for (uint index = 0u; index < min(cutLineCount, kCutMaxLines); ++index)
    {
        float4 cutLine = cutLines[index];
        bool positive = dot(pixel - cutLine.xy * float2(kCutAspect, 1.0f), cutLine.zw) >= 0.0f;
        if (positive != ((mask & (1u << index)) != 0u))
        {
            return false;
        }
    }
    return true;
}

// 回りながら戻ってくる斬線の破片を、中心の分かっている破片から総当たりで探す（重なったら先にはまる方を手前に）。
// 中心の表に載らなかった小さな破片は、回さずに割れた時と同じ探し方で戻す。
bool FindCutShardTurning(float2 uv, float breakT, out uint mask, out float2 homeUV)
{
    float2 aspect = float2(kCutAspect, 1.0f);
    float bestFall = kFallNotFound;
    bool found = false;
    mask = 0u;
    homeUV = uv;
    [loop]
    for (uint index = 0u; index < min(cutPieceCount, kMaxCutPieces); ++index)
    {
        uint pieceMask = CutPieceMask(index);
        float fall = CutFallProgress(pieceMask, breakT);
        float4 piece = cutPieceCenters[index];
        // 回しても半径の外へは出ないので、落下で動いた中心から遠い破片は先に外す
        float2 movedCenter = piece.xy * aspect + CutFallOffset(pieceMask, fall);
        if (fall >= bestFall || length(uv * aspect - movedCenter) > piece.z + cutSlide)
        {
            continue;
        }
        float angle = ReassembleAngle(CutHash(pieceMask ^ kCutSpinSeed), fall);
        float2 home = InverseRigid(uv, piece.xy, angle, CutDisplacement(pieceMask, breakT));
        if (all(home >= 0.0f) && all(home <= 1.0f) && InCutShard(home, pieceMask))
        {
            found = true;
            bestFall = fall;
            mask = pieceMask;
            homeUV = home;
        }
    }
    // || は両辺とも実行されて見つけた結果を上書きしうるので、if で分ける
    if (found)
    {
        return true;
    }
    return FindCutShard(uv, breakT, mask, homeUV);
}

// 放射状の破片を、割れた時刻 breakT の位置から回しながら戻す。
float4 DrawGlassReassemble(float2 uv, float breakT, float t)
{
    float3 cell;
    float2 homeUV;
    if (!FindGlassShardTurning(uv, breakT, cell, homeUV))
    {
        return float4(kVoidColor, 1.0f);
    }
    float edge = VoronoiEdge(GlassSpace(homeUV, uint(cell.z)));
    float rim = 1.0f - smoothstep(0.0f, kGlassRimWidth, edge);
    float crack = 1.0f - smoothstep(0.0f, kGlassCrackWidth, edge);
    // 落ち始めた時刻まで巻き戻った所ではまる
    float landing = kSeparateEnd + GlassFallDelay(cell) * (1.0f - kSeparateEnd);
    return ShadeReassembling(GlassFallProgress(cell, breakT), landing - breakT, rim, crack, hash1(cell.xy), t);
}

// 斬線の破片を、割れた時刻 breakT の位置から戻す（縁とひびは斬線の上だけ）。
float4 DrawCutReassemble(float2 uv, float breakT, float t)
{
    uint mask;
    float2 homeUV;
    if (!FindCutShardTurning(uv, breakT, mask, homeUV))
    {
        return float4(kVoidColor, 1.0f);
    }
    float edge = CutEdgeDistance(homeUV);
    float rim = 1.0f - smoothstep(0.0f, kCutRimWidth, edge);
    float crack = 1.0f - smoothstep(0.0f, kCutLandCrackWidth, edge);
    float landing = cutSeparateEnd + CutFallDelay(mask) * (1.0f - cutSeparateEnd);
    return ShadeReassembling(CutFallProgress(mask, breakT), landing - breakT, rim, crack, CutHash(mask ^ kSpreadSeed), t);
}

// 割れた動きを最後（落ち切った所）から最初（割れる前）まで巻き戻し、割れた時と同じ形の破片で集まり直す。
float4 DrawReassemble(float2 uv, float t)
{
    float breakT = 1.0f - saturate(t / kRewindEnd);
    return cutLineCount > 0u ? DrawCutReassemble(uv, breakT, t) : DrawGlassReassemble(uv, breakT, t);
}

// 割れるだけか、割れた後に集まり直すかで描き分ける。
float4 main(PSInput input) : SV_Target
{
    if (reassemble == 0u)
    {
        return DrawBreak(input.uv, time);
    }
    if (time < kBreakEnd)
    {
        // 破片の抜けた所は後ろの画面ではなく暗い空間にする
        float4 shard = DrawBreak(input.uv, time / kBreakEnd);
        return float4(lerp(kVoidColor, shard.rgb, shard.a), 1.0f);
    }
    return DrawReassemble(input.uv, saturate((time - kAssembleStart) / (1.0f - kAssembleStart)));
}
