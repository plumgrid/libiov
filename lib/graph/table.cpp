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

#include "libiov/table.h"
#include <arpa/inet.h>
#include <string.h>
#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>
#include "libiov/event.h"
#include "libiov/filesystem.h"
#include "libiov/internal/types.h"
#include "libiov/module.h"

using std::future;
using std::promise;
using std::string;
using std::vector;
using std::unique_ptr;

using namespace iov::internal;

namespace iov {

Table::Table() {}
Table::Table(const string name, bool scope, size_t s_key, size_t s_leaf) {
  table_name = name;
  global = scope;
  key_size = s_key;
  leaf_size = s_leaf;
}
Table::Table(const string name, bool scope) {
  table_name = name;
  global = scope;
}
Table::~Table() {}

bool Table::InitTable(IOModule *module, string t_file, string m_file) {
  FileSystem *fs = module->GetFileSystemHandler();

  tableprog_.reset(new FileDesc(fs->Open(t_file.c_str())));
  if (*tableprog_ < 0)
    return false;

  fd_table = *tableprog_;

  metaprog_.reset(new FileDesc(fs->Open(m_file.c_str())));

  if (*metaprog_ < 0)
    return false;

  fd_meta = *metaprog_;

  return true;
}

bool Table::InitTable(IOModule *module, size_t index) {
  string p;
  int ret;
  ebpf::BPFModule *bpf_mod = module->GetBpfModule();
  FileSystem *fs = module->GetFileSystemHandler();

  tableprog_.reset(
      new FileDesc(Insert((bpf_map_type)(bpf_mod->table_type(index)), key_size,
          leaf_size, bpf_mod->table_max_entries(index))));
  if (*tableprog_ < 0)
    return false;

  fd_table = *tableprog_;

  if (!fs->MakePathName(p, module, TABLE, table_name, global)) {
    std::cout << "Create dir for table failed" << std::endl;
    return false;
  }

  p += table_name;
  path_table_fd = p;
  p += "_metadata";
  path_meta_fd = p;

  ret = fs->Save(path_table_fd, *tableprog_);
  if (ret < 0) {
    std::cout << "Failed to pin: " << bpf_mod->table_name(index) << std::endl;
    return false;
  }

  struct descr item;
  item.key_size = key_size;
  item.leaf_size = leaf_size;
  metaprog_.reset(new FileDesc(
      Insert(BPF_MAP_TYPE_HASH, sizeof(uint32_t), sizeof(struct descr), 1)));

  if (*metaprog_ < 0)
    return false;

  fd_meta = *metaprog_;

  ret = fs->Save(path_meta_fd, *metaprog_);

  uint32_t key = 0;
  Update(META, &key, &item, BPF_ANY);

  return true;
}

// Insert creates a Map of type bpf_map_type, with key size key_size, a value
// size of leaf_size and the maximum amount of entries of max_entries.

int Table::Insert(
    bpf_map_type map_type, int key_size, int leaf_size, int max_entries) {
  int fd;
  fd = bpf_create_map(map_type, key_size, leaf_size, max_entries);
  return fd;
}

// Update updates the map in fd with the given value in the given key.
int Table::Update(obj_type_t type, void *key, void *value, uint64_t flags) {
  int fd, ret;
  switch (type) {
  case TABLE:
    fd = *tableprog_;
    break;
  case META:
    fd = *metaprog_;
    break;
  default:
    std::cout << "Not a valid obj type" << std::endl;
    return -1;
  }
  ret = bpf_update_elem(fd, key, value, flags);

  return ret;
}

// Lookup looks up for the map value stored in fd with the given key.
// The value is stored in the value pointer.
int Table::Lookup(obj_type_t type, void *key, void *value) {
  int ret, fd;
  switch (type) {
  case TABLE:
    fd = *tableprog_;
    break;
  case META:
    fd = *metaprog_;
    break;
  default:
    std::cout << "Not a valid obj type" << std::endl;
    return -1;
  }

  ret = bpf_lookup_elem(fd, key, value);
  return ret;
}

// Delete deletes the map element with the given key.
int Table::Delete(obj_type_t type, void *key) {
  int ret, fd;
  switch (type) {
  case TABLE:
    fd = *tableprog_;
    break;
  case META:
    fd = *metaprog_;
    break;
  default:
    std::cout << "Not a valid obj type" << std::endl;
    return -1;
  }
  ret = bpf_delete_elem(fd, key);
  return ret;
}

// GetKey stores, in next_key, the next key after the key of the map in fd.
int Table::GetKey(obj_type_t type, void *key, void *next_key) {
  int ret, fd;
  switch (type) {
  case TABLE:
    fd = *tableprog_;
    break;
  case META:
    fd = *metaprog_;
    break;
  default:
    std::cout << "Not a valid obj type" << std::endl;
    return -1;
  }

  ret = bpf_get_next_key(fd, key, next_key);
  return ret;
}

int Table::GetTableElements(std::map<std::string, std::string> &element) {
  // Open Metadata
  // Open Key desc optional for now
  // Open Leaf desc optional for now
  // lookup all the keys and return them along with values
  int ret = 0;
  uint32_t key_meta;
  struct descr item;

  key_meta = 0;
  ret = Lookup(META, &key_meta, &item);
  if (ret != 0) {
    return ret;
  }

  std::string key(item.key_size, 'f');
  std::string next_key(item.key_size, '\0');
  std::string leaf(item.leaf_size, '\0');
  for (;;) {
    ret = GetKey(TABLE, (void *)key.c_str(), (void *)next_key.c_str());

    if (ret != 0) {
      // Should return a value for this that is not 0
      // since empty table are ok. Set it to 0 for now.
      // We'll have to think about error handling later.
      ret = 0;
      break;
    }

    ret = Lookup(TABLE, (void *)next_key.c_str(), (void *)leaf.c_str());
    if (ret != 0) {
      return ret;
    }
    element[next_key] = leaf;
    // We can fill the data
    key = next_key;
  }
  return ret;
}

void Table::DumpItem(std::string item) {
  for (std::string::iterator it = item.begin(); it != item.end(); ++it) {
    int ch = static_cast<unsigned char>(*it);
    std::cout << std::showbase       // show the 0x prefix
              << std::internal       // fill between the prefix and the number
              << std::setfill('0');  // fill with 0s

    std::cout << std::hex << std::setw(2) << ch << " ";
  }
  std::cout << std::endl;
}

int Table::ShowTableElements() {
  std::map<std::string, std::string> items;
  int ret = 0;
  ret = GetTableElements(items);
  if (ret != 0)
    return ret;
  std::map<std::string, std::string>::iterator it = items.begin();
  while (it != items.end()) {
    // Accessing KEY from element pointed by it.
    std::string key = it->first;
    DumpItem(it->first);
    // Accessing VALUE from element pointed by it.
    std::string value = it->second;
    DumpItem(it->second);
    std::cout << std::endl;
    // Increment the Iterator to point to next entry
    it++;
  }
  return ret;
}

void Table::SetTableScope(bool scope) { global = scope; }

bool Table::GetTableScope() { return global; }

string Table::GetTableFdPath() { return path_table_fd; }
string Table::GetMetaFdPath() { return path_meta_fd; }

int Table::GetTableFileDescriptor() {
  FileDesc *fd = tableprog_.get();
  return *fd;
}
int Table::GetMetaFileDescriptor() {
  FileDesc *fd = metaprog_.get();
  return *fd;
}

}  // End of namespace iov
