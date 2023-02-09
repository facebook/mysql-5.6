/*
  Copyright (C) 2022-2023 Laurynas Biveinis

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#ifndef CLONE_COMMON_H
#define CLONE_COMMON_H

#include "sql/handler.h"

namespace myclone {

class Ha_clone_common_cbk : public Ha_clone_cbk {
 public:
  [[nodiscard]] int precopy(THD *thd, uint task_id) override;

  [[nodiscard]] int synchronize_engines() override;
};

}  // namespace myclone

#endif  // CLONE_COMMON_H
