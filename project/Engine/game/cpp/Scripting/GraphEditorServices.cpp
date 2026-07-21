/**
 * @file GraphEditorServices.cpp
 * @brief グラフ編集の入力処理とノード表示を個別に実行する
 */
#ifdef USE_IMGUI
#include "GraphEditorServices.h"
#include "GraphEditor.h"

namespace engine::game {
void GraphEditorInteraction::Update(GraphEditor& editor, engine::Input* input)
{
    editor.UpdateContent(input);
}

void GraphNodeRenderer::Draw(GraphEditor& editor, ImDrawList* drawList, const ImVec2& origin,
    const std::string& id, GraphNode& node, void* state)
{
    editor.DrawNodeContent(drawList, origin, id, node,
        *static_cast<GraphEditor::CanvasFrameState*>(state));
}
} // namespace engine::game
#endif
