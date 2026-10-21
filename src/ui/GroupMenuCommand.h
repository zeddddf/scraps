#pragma once

#include <cstddef>

namespace clipd2d::ui {

enum class GroupMenuCommandKind {
  None = 0,
  SelectLibrary,
  SelectExistingGroup,
  CreateGroup,
  RenameGroup,
  DeleteGroup,
};

struct GroupMenuCommand {
  GroupMenuCommandKind kind = GroupMenuCommandKind::None;
  size_t group_index = 0;
};

inline GroupMenuCommand DecodeGroupMenuCommand(int command, size_t group_count) {
  if (command == 1) {
    return GroupMenuCommand{GroupMenuCommandKind::SelectLibrary, 0};
  }
  if (command == 2000) {
    return GroupMenuCommand{GroupMenuCommandKind::CreateGroup, 0};
  }
  if (command >= 3000 && command < 3000 + static_cast<int>(group_count)) {
    return GroupMenuCommand{
        GroupMenuCommandKind::RenameGroup,
        static_cast<size_t>(command - 3000)};
  }
  if (command >= 4000 && command < 4000 + static_cast<int>(group_count)) {
    return GroupMenuCommand{
        GroupMenuCommandKind::DeleteGroup,
        static_cast<size_t>(command - 4000)};
  }
  if (command >= 10 && command < 10 + static_cast<int>(group_count)) {
    return GroupMenuCommand{
        GroupMenuCommandKind::SelectExistingGroup,
        static_cast<size_t>(command - 10)};
  }
  return GroupMenuCommand{};
}

}  // namespace clipd2d::ui
