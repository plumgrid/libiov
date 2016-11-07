/*
 * Copyright (c) 2016, PLUMgrid, http://plumgrid.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string.h>
#include "libiov/internal/types.h"
#include "libiov/module.h"
#include "libiov/prog.h"
#include "libiov/metadata.h"
#include "libiov/table.h"
#include "libiov/filesystem.h"
#include "libiov/event.h"

using std::future;
using std::promise;
using std::string;
using namespace iov::internal;

namespace iov {

MetaData::MetaData() {}
MetaData::~MetaData() {
}

void MetaData::Update(ebpf::BPFModule *bpf_mod) {
  std::string desc = bpf_mod->table_key_desc(0);
  item.key_size = bpf_mod->table_key_size(0);
  item.key_desc_size = desc.length();

  desc = bpf_mod->table_leaf_desc(0);
  item.leaf_size = bpf_mod->table_leaf_size(0);
  item.leaf_desc_size = desc.length();
}
} //End of namespace iov

