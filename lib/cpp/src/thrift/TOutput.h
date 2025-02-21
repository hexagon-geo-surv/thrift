/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements. See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership. The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef _THRIFT_OUTPUT_H_
#define _THRIFT_OUTPUT_H_ 1

//#include <thrift/thrift_export.h>

namespace apache {
namespace thrift {

class TOutput {
public:
  TOutput();

  inline void setOutputFunction(void (*function)(const char*)) { f_ = function; }

  inline void operator()(const char* message) { f_(message); }

  // It is important to have a const char* overload here instead of
  // just the string version, otherwise errno could be corrupted
  // if there is some problem allocating memory when constructing
  // the string.
  void perror(const char* message, int errno_copy);
  inline void perror(const std::string& message, int errno_copy) {
    perror(message.c_str(), errno_copy);
  }

  void printf(const char* message, ...);

  static void errorTimeWrapper(const char* msg);

  /** Just like strerror_r but returns a C++ string object. */
  static std::string strerror_s(int errno_copy);

private:
  void (*f_)(const char*);
};

/*THRIFT_EXPORT*/ extern TOutput GlobalOutput;   // if you need this exported, build your own wrapper lib around and export it yourself
}
} // namespace apache::thrift

#endif //_THRIFT_OUTPUT_H_
