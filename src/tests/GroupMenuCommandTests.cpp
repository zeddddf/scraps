#include "ui/GroupMenuCommand.h"

#include "tests/TestSupport.h"

namespace clipd2d::tests {

CLIPD2D_TEST(GroupMenuCommandDecodeTreatsCreateAsDedicatedCommand) {
  const auto command = clipd2d::ui::DecodeGroupMenuCommand(2000, 3);
  CLIPD2D_CHECK_EQ(command.kind, clipd2d::ui::GroupMenuCommandKind::CreateGroup);
}

CLIPD2D_TEST(GroupMenuCommandDecodeTreatsRenameAndDeleteAsDedicatedCommands) {
  auto command = clipd2d::ui::DecodeGroupMenuCommand(3001, 4);
  CLIPD2D_CHECK_EQ(command.kind, clipd2d::ui::GroupMenuCommandKind::RenameGroup);
  CLIPD2D_CHECK_EQ(command.group_index, static_cast<size_t>(1));

  command = clipd2d::ui::DecodeGroupMenuCommand(4002, 4);
  CLIPD2D_CHECK_EQ(command.kind, clipd2d::ui::GroupMenuCommandKind::DeleteGroup);
  CLIPD2D_CHECK_EQ(command.group_index, static_cast<size_t>(2));
}

CLIPD2D_TEST(GroupMenuCommandDecodeKeepsExistingGroupSelectionBounded) {
  auto command = clipd2d::ui::DecodeGroupMenuCommand(12, 5);
  CLIPD2D_CHECK_EQ(command.kind, clipd2d::ui::GroupMenuCommandKind::SelectExistingGroup);
  CLIPD2D_CHECK_EQ(command.group_index, static_cast<size_t>(2));

  command = clipd2d::ui::DecodeGroupMenuCommand(12, 2);
  CLIPD2D_CHECK_EQ(command.kind, clipd2d::ui::GroupMenuCommandKind::None);
}

}  // namespace clipd2d::tests
