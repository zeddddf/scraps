#include "tests/TestSupport.h"

#include "editing/InteractionStateMachine.h"

// InteractionStateMachine 的 P3-17 模式级转移表（ESC / 右键 / 双击梯级语义）
// 是截图交互的单一事实来源。这里不建真窗口：构造后直接灌事件序列，
// 断言状态迁移与 Action 返回值。构造会枚举本机窗口拓扑（副作用但无窗口创建）。

namespace clipd2d::tests {
namespace {

using Action = InteractionStateMachine::Action;
using Mode = InteractionStateMachine::Mode;

Action FeedKey(InteractionStateMachine& ism, int virtualKey) {
  return ism.ProcessMouseInputEx(WM_KEYDOWN, PointPx{virtualKey, 0});
}

Action FeedMouse(InteractionStateMachine& ism, UINT msg, int x, int y) {
  return ism.ProcessMouseInputEx(msg, PointPx{x, y});
}

// 进入文本编辑态：选区内 Text 工具点击会新建文本框。
void EnterTextEditing(InteractionStateMachine& ism, int x, int y) {
  ism.SetMode(Mode::Text);
  FeedMouse(ism, WM_LBUTTONDOWN, x, y);
  CLIPD2D_CHECK(ism.IsEditingText());
}

}  // namespace

// ESC 梯级：① 工具态退回 SelectRoi（不退出）；② SelectRoi 请求退出截图。
CLIPD2D_TEST(IsmEscapeLadderBacksOutToolThenRequestsCancel) {
  InteractionStateMachine ism;
  ism.SetMode(Mode::DrawRectangle);
  CLIPD2D_CHECK_EQ(ism.GetMode(), Mode::DrawRectangle);
  CLIPD2D_CHECK(ism.GuardInToolMode());

  CLIPD2D_CHECK_EQ(FeedKey(ism, VK_ESCAPE), Action::None);
  CLIPD2D_CHECK_EQ(ism.GetMode(), Mode::SelectRoi);
  CLIPD2D_CHECK(!ism.GuardInToolMode());

  CLIPD2D_CHECK_EQ(FeedKey(ism, VK_ESCAPE), Action::Cancel);
}

// 右键梯级：① 绘制中丢草稿；② 有选区清选区；③ 兜底请求退出。
CLIPD2D_TEST(IsmRightClickLadderDiscardsDraftClearsSelectionThenCancels) {
  InteractionStateMachine ism;

  // ① 绘制中（矩形草稿在 MOUSEMOVE 时生成）。
  ism.SetMode(Mode::DrawRectangle);
  FeedMouse(ism, WM_LBUTTONDOWN, 10, 10);
  FeedMouse(ism, WM_MOUSEMOVE, 50, 40);
  CLIPD2D_CHECK(ism.GetCurrentDraft() != nullptr);
  CLIPD2D_CHECK(ism.GuardIsDrawingDraft());

  CLIPD2D_CHECK_EQ(FeedMouse(ism, WM_RBUTTONDOWN, 50, 40), Action::None);
  CLIPD2D_CHECK(ism.GetCurrentDraft() == nullptr);
  CLIPD2D_CHECK(!ism.GuardIsDrawingDraft());

  // ② 有选区 → 清选区，退回 SelectRoi。
  ism.GetSelection().SetRoi(RectPx{80, 80, 200, 150});
  CLIPD2D_CHECK(ism.GuardHasSelection());
  CLIPD2D_CHECK_EQ(FeedMouse(ism, WM_RBUTTONDOWN, 100, 100), Action::None);
  CLIPD2D_CHECK(!ism.GuardHasSelection());
  CLIPD2D_CHECK_EQ(ism.GetSelection().GetState(), SelectionModel::State::None);

  // ③ 无草稿无选区 → 请求退出。
  CLIPD2D_CHECK_EQ(FeedMouse(ism, WM_RBUTTONDOWN, 100, 100), Action::Cancel);
}

// ESC① 文本编辑态：非空草稿提交为标注，空草稿直接丢弃（C2：绝不静默丢非空文本）。
CLIPD2D_TEST(IsmEscapeCommitsNonEmptyTextAndDiscardsEmpty) {
  InteractionStateMachine ism;
  ism.GetSelection().SetRoi(RectPx{50, 50, 400, 300});

  // 非空 → 提交。
  EnterTextEditing(ism, 60, 60);
  CLIPD2D_CHECK(ism.HandleKeyboardMessage(nullptr, WM_CHAR, L'A', 0));
  CLIPD2D_CHECK(ism.HandleKeyboardMessage(nullptr, WM_CHAR, L'B', 0));
  CLIPD2D_CHECK(ism.GetActiveText() == L"AB");
  CLIPD2D_CHECK_EQ(FeedKey(ism, VK_ESCAPE), Action::None);
  CLIPD2D_CHECK(!ism.IsEditingText());
  {
    const auto hit = ism.GetDocument().HitTestText(D2D1::Point2F(70.0f, 70.0f));
    CLIPD2D_CHECK(hit != nullptr);
    CLIPD2D_CHECK(hit->text == L"AB");
  }

  // 空草稿 → 不落标注（在 A1 下方建框，避免点进 A1 的 rect）。
  EnterTextEditing(ism, 60, 200);
  CLIPD2D_CHECK_EQ(FeedKey(ism, VK_ESCAPE), Action::None);
  CLIPD2D_CHECK(!ism.IsEditingText());
  CLIPD2D_CHECK(ism.GetDocument().HitTestText(D2D1::Point2F(70.0f, 210.0f)) == nullptr);
  // 先前的标注不受影响。
  CLIPD2D_CHECK(ism.GetDocument().HitTestText(D2D1::Point2F(70.0f, 70.0f)) != nullptr);
}

// U5：双击只撤销「本手势提交过的草稿」，且撤销动作确实落库。
CLIPD2D_TEST(IsmDoubleClickUndoesGestureDraftOnly) {
  InteractionStateMachine ism;
  ism.GetSelection().SetRoi(RectPx{50, 50, 400, 300});

  // 两条文本标注 A1/A2 先落库。
  EnterTextEditing(ism, 60, 60);
  CLIPD2D_CHECK(ism.HandleKeyboardMessage(nullptr, WM_CHAR, L'1', 0));
  FeedKey(ism, VK_ESCAPE);
  EnterTextEditing(ism, 60, 150);
  CLIPD2D_CHECK(ism.HandleKeyboardMessage(nullptr, WM_CHAR, L'2', 0));
  FeedKey(ism, VK_ESCAPE);
  CLIPD2D_CHECK(ism.GetDocument().HitTestText(D2D1::Point2F(70.0f, 70.0f)) != nullptr);
  CLIPD2D_CHECK(ism.GetDocument().HitTestText(D2D1::Point2F(70.0f, 160.0f)) != nullptr);

  // 画一笔矩形草稿并提交（document 栈顶成为 B）。
  ism.SetMode(Mode::DrawRectangle);
  FeedMouse(ism, WM_LBUTTONDOWN, 200, 220);
  FeedMouse(ism, WM_MOUSEMOVE, 280, 270);
  FeedMouse(ism, WM_LBUTTONUP, 280, 270);
  CLIPD2D_CHECK(ism.GuardShouldUndoGestureDraft());

  // 双击在选区内部：撤销手势草稿 B → A1/A2 都必须还在。
  CLIPD2D_CHECK_EQ(FeedMouse(ism, WM_LBUTTONDBLCLK, 220, 230), Action::Complete);
  CLIPD2D_CHECK(!ism.GuardShouldUndoGestureDraft());
  CLIPD2D_CHECK(ism.GetDocument().HitTestText(D2D1::Point2F(70.0f, 70.0f)) != nullptr);
  CLIPD2D_CHECK(ism.GetDocument().HitTestText(D2D1::Point2F(70.0f, 160.0f)) != nullptr);
}

// U5 反向：本手势没提交过草稿时，空双击绝不能动已有标注。
CLIPD2D_TEST(IsmEmptyDoubleClickKeepsExistingAnnotations) {
  InteractionStateMachine ism;
  ism.GetSelection().SetRoi(RectPx{50, 50, 400, 300});

  EnterTextEditing(ism, 60, 60);
  CLIPD2D_CHECK(ism.HandleKeyboardMessage(nullptr, WM_CHAR, L'X', 0));
  FeedKey(ism, VK_ESCAPE);
  CLIPD2D_CHECK(ism.GetDocument().HitTestText(D2D1::Point2F(70.0f, 70.0f)) != nullptr);
  CLIPD2D_CHECK(!ism.GuardShouldUndoGestureDraft());

  CLIPD2D_CHECK_EQ(FeedMouse(ism, WM_LBUTTONDBLCLK, 220, 230), Action::Complete);
  CLIPD2D_CHECK(ism.GetDocument().HitTestText(D2D1::Point2F(70.0f, 70.0f)) != nullptr);
}

// 转移表 guard 谓词与机器状态的同步性（表行匹配的依据）。
CLIPD2D_TEST(IsmGuardsTrackModeSelectionAndDraft) {
  InteractionStateMachine ism;
  CLIPD2D_CHECK(!ism.GuardInToolMode());
  CLIPD2D_CHECK(!ism.GuardHasSelection());
  CLIPD2D_CHECK(!ism.GuardIsDrawingDraft());
  CLIPD2D_CHECK(!ism.GuardIsEditingText());
  CLIPD2D_CHECK(!ism.GuardShouldUndoGestureDraft());

  ism.SetMode(Mode::DrawArrow);
  CLIPD2D_CHECK(ism.GuardInToolMode());

  ism.GetSelection().SetRoi(RectPx{0, 0, 100, 100});
  CLIPD2D_CHECK(ism.GuardHasSelection());

  ism.SetMode(Mode::Text);
  FeedMouse(ism, WM_LBUTTONDOWN, 10, 10);
  CLIPD2D_CHECK(ism.GuardIsEditingText());

  ism.Reset();
  CLIPD2D_CHECK_EQ(ism.GetMode(), Mode::SelectRoi);
  CLIPD2D_CHECK(!ism.GuardHasSelection());
  CLIPD2D_CHECK(!ism.GuardIsEditingText());
}

}  // namespace clipd2d::tests
