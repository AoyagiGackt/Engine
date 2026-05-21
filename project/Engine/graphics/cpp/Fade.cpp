#include "Fade.h"

void Fade::Initialize(SpriteCommon* spriteCommon)
{
    // 画面全体を覆う黒いスプライトを作成
    // テクスチャは白い軽画像を使って色を黒にする
    sprite_ = std::make_unique<Sprite>();
    sprite_->Initialize(spriteCommon, "Resources/uvChecker.png");

    sprite_->SetPosition({ 0.0f, 0.0f });
    sprite_->SetSize({ 1280.0f, 720.0f }); // 画面解像度に合わせる
    sprite_->SetColor({ 0.0f, 0.0f, 0.0f, 0.0f }); // 最初は透明な黒
}

void Fade::Start(Status status, float duration)
{
    status_ = status;
    duration_ = duration;
    counter_ = 0.0f;
}

void Fade::Update(){
    if(status_ == Status::None){
        return;
    }

    // タイマーを進める
    counter_ += 1.0f / 60.0f;

    // 進捗率 (0.0f ～ 1.0f)
    float progress = counter_ / duration_;
    
    if(progress > 1.0f){ 
        progress = 1.0f;
    }

    float alpha = 0.0f;
    
    if(status_ == Status::FadeIn){
        alpha = 1.0f - progress; // 1.0(黒) -> 0.0(透明)
    } else if(status_ == Status::FadeOut){
        alpha = progress;        // 0.0(透明) -> 1.0(黒)
    }

    // スプライトに色を反映
    sprite_->SetColor({0.0f, 0.0f, 0.0f, alpha});
    sprite_->Update();

    // 進捗が1.0を超えたら終了状態にする（色の反映が終わった後に判定）
    if(progress >= 1.0f){
        status_ = Status::None;
    }
}

void Fade::Draw()
{
    // アルファ値が0なら描画しない（負荷軽減）
    if (sprite_->GetColor().w <= 0.0f) {
        return;
    }

    // 前のターンで作ったアルファブレンドの設定で描画
    sprite_->Draw();
}