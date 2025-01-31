/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_INTERNAL_COMPILER_RUBY_GENERATOR_STRING_INL_H
#define GRPC_INTERNAL_COMPILER_RUBY_GENERATOR_STRING_INL_H

#include <algorithm>
#include <sstream>
#include <vector>

#include "src/compiler/config.h"

using std::getline;
using std::transform;

namespace grpc_ruby_generator {

// Split splits a string using char into elems.
inline std::vector<TString>& Split(const TString& s, char delim,
                                       std::vector<TString>* elems) {
  std::stringstream ss(s);
  TString item;
  while (getline(ss, item, delim)) {
    elems->push_back(item);
  }
  return *elems;
}

// Split splits a string using char, returning the result in a vector.
inline std::vector<TString> Split(const TString& s, char delim) {
  std::vector<TString> elems;
  Split(s, delim, &elems);
  return elems;
}

// Replace replaces from with to in s.
inline TString Replace(TString s, const TString& from,
                           const TString& to) {
  size_t start_pos = s.find(from);
  if (start_pos == TString::npos) {
    return s;
  }
  s.replace(start_pos, from.length(), to);
  return s;
}

// ReplaceAll replaces all instances of search with replace in s.
inline TString ReplaceAll(TString s, const TString& search,
                              const TString& replace) {
  size_t pos = 0;
  while ((pos = s.find(search, pos)) != TString::npos) {
    s.replace(pos, search.length(), replace);
    pos += replace.length();
  }
  return s;
}

// ReplacePrefix replaces from with to in s if search is a prefix of s.
inline bool ReplacePrefix(TString* s, const TString& from,
                          const TString& to) {
  size_t start_pos = s->find(from);
  if (start_pos == TString::npos || start_pos != 0) {
    return false;
  }
  s->replace(start_pos, from.length(), to);
  return true;
}

// Modularize converts a string into a ruby module compatible name
inline TString Modularize(TString s) {
  if (s.empty()) {
    return s;
  }
  TString new_string = "";
  bool was_last_underscore = false;
  new_string.append(1, ::toupper(s[0]));
  for (TString::size_type i = 1; i < s.size(); ++i) {
    if (was_last_underscore && s[i] != '_') {
      new_string.append(1, ::toupper(s[i]));
    } else if (s[i] != '_') {
      new_string.append(1, s[i]);
    }
    was_last_underscore = s[i] == '_';
  }
  return new_string;
}

// RubyPackage gets the ruby package in either proto or ruby_package format
inline TString RubyPackage(const grpc::protobuf::FileDescriptor* file) {
  TString package_name = file->package();
  if (file->options().has_ruby_package()) {
    package_name = file->options().ruby_package();

    // If :: is in the package convert the Ruby formatted name
    //    -> A::B::C
    // to use the dot seperator notation
    //    -> A.B.C
    package_name = ReplaceAll(package_name, "::", ".");
  }
  return package_name;
}

// RubyTypeOf updates a proto type to the required ruby equivalent.
inline TString RubyTypeOf(const grpc::protobuf::Descriptor* descriptor) {
  TString proto_type = descriptor->full_name();
  if (descriptor->file()->options().has_ruby_package()) {
    // remove the leading package if present
    ReplacePrefix(&proto_type, descriptor->file()->package(), "");
    ReplacePrefix(&proto_type, ".", "");  // remove the leading . (no package)
    proto_type = RubyPackage(descriptor->file()) + "." + proto_type;
  }
  TString res("." + proto_type);
  if (res.find('.') == TString::npos) {
    return res;
  } else {
    std::vector<TString> prefixes_and_type = Split(res, '.');
    res.clear();
    for (unsigned int i = 0; i < prefixes_and_type.size(); ++i) {
      if (i != 0) {
        res += "::";  // switch '.' to the ruby module delim
      }
      if (i < prefixes_and_type.size() - 1) {
        res += Modularize(prefixes_and_type[i]);  // capitalize pkgs
      } else {
        res += prefixes_and_type[i];
      }
    }
    return res;
  }
}

}  // namespace grpc_ruby_generator

#endif  // GRPC_INTERNAL_COMPILER_RUBY_GENERATOR_STRING_INL_H
