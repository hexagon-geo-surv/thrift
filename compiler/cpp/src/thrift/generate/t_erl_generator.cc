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

#include <string>
#include <fstream>
#include <iostream>
#include <limits>
#include <vector>

#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sstream>
#include "thrift/platform.h"
#include "thrift/version.h"
#include "thrift/generate/t_generator.h"

using std::map;
using std::ofstream;
using std::ostream;
using std::ostringstream;
using std::string;
using std::stringstream;
using std::vector;

/**
 * Helper enum for string mapping
 */
enum class StringTo {String, Binary, Both};

/**
 * Helper enum for sets mapping
 */
enum class SetsTo {V1, V2};

/**
 * Erlang code generator.
 *
 */
class t_erl_generator : public t_generator {
public:
  t_erl_generator(t_program* program,
                  const std::map<std::string, std::string>& parsed_options,
                  const std::string& option_string)
    : t_generator(program) {
    (void)option_string;
    std::map<std::string, std::string>::const_iterator iter;

    legacy_names_ = false;
    delimiter_ = ".";
    app_prefix_ = "";
    maps_ = false;
    export_lines_first_ = true;
    export_types_lines_first_ = true;
    string_to_ = StringTo::Both;
    sets_to_ = SetsTo::V1;

    for( iter = parsed_options.begin(); iter != parsed_options.end(); ++iter) {
      if( iter->first.compare("legacynames") == 0) {
        legacy_names_ = true;
      } else if( iter->first.compare("maps") == 0) {
        maps_ = true;
      } else if( iter->first.compare("delimiter") == 0) {
        delimiter_ = iter->second;
      } else if( iter->first.compare("app_prefix") == 0) {
        app_prefix_ = iter->second;
      } else if( iter->first.compare("string") == 0) {
        auto string_to_value = iter->second;
        if( string_to_value.compare("string") == 0) {
          string_to_ = StringTo::String;
        } else if( string_to_value.compare("binary") == 0) {
          string_to_ = StringTo::Binary;
        } else if( string_to_value.compare("both") == 0) {
          string_to_ = StringTo::Both;
        } else {
          throw "unknown string option value:" + string_to_value;
        }
      } else if( iter->first.compare("set") == 0) {
        auto set_to_value = iter->second;
        if( set_to_value.compare("v1") == 0) {
          sets_to_ = SetsTo::V1;
        } else if( set_to_value.compare("v2") == 0) {
          sets_to_ = SetsTo::V2;
        } else {
          throw "unknown set option value:" + set_to_value;
        }
      } else {
        throw "unknown option erl:" + iter->first;
      }
    }

    out_dir_base_ = "gen-erl";
  }

  /**
   * Init and close methods
   */

  void init_generator() override;
  void close_generator() override;
  std::string display_name() const override;

  /**
   * Program-level generation functions
   */

  void generate_typedef(t_typedef* ttypedef) override;
  void generate_enum(t_enum* tenum) override;
  void generate_const(t_const* tconst) override;
  void generate_struct(t_struct* tstruct) override;
  void generate_xception(t_struct* txception) override;
  void generate_service(t_service* tservice) override;
  void generate_member_type(std::ostream& out, t_type* type);
  void generate_member_value(std::ostream& out, t_type* type, t_const_value* value);

  std::string render_member_type(t_field* field);
  std::string render_member_value(t_field* field);
  std::string render_member_requiredness(t_field* field);
  std::string render_string_type();

  //  std::string render_default_value(t_type* type);
  std::string render_default_value(t_field* field);
  std::string render_const_value(t_type* type, t_const_value* value);
  std::string render_type_term(t_type* ttype, bool expand_structs, bool extended_info = false);
  std::string render_default_sets_value();

  /**
   * Struct generation code
   */

  void generate_erl_struct(t_struct* tstruct, bool is_exception);
  void generate_erl_struct_definition(std::ostream& out, t_struct* tstruct);
  void generate_erl_struct_member(std::ostream& out, t_field* tmember);
  void generate_erl_struct_info(std::ostream& out, t_struct* tstruct);
  void generate_erl_extended_struct_info(std::ostream& out, t_struct* tstruct);
  void generate_erl_function_helpers(t_function* tfunction);
  void generate_type_metadata(std::string function_name, vector<string> names);
  void generate_enum_info(t_enum* tenum);
  void generate_enum_metadata();
  void generate_const_function(t_const* tconst, ostringstream& exports, ostringstream& functions);
  void generate_const_functions();

  /**
   * Service-level generation functions
   */

  void generate_service_helpers(t_service* tservice);
  void generate_service_metadata(t_service* tservice);
  void generate_service_interface(t_service* tservice);
  void generate_function_info(t_service* tservice, t_function* tfunction);

  /**
   * Helper rendering functions
   */

  std::string erl_autogen_comment();
  std::string erl_imports();
  std::string render_includes();
  std::string type_name(t_type* ttype);
  std::string render_const_list_values(t_type* type, t_const_value* value);
  std::string render_const_string_value(t_const_value* value);

  std::string function_signature(t_function* tfunction, std::string prefix = "");

  std::string argument_list(t_struct* tstruct);
  std::string type_to_enum(t_type* ttype);
  std::string type_module(t_type* ttype);

  std::string make_safe_for_module_name(std::string in) {
    if (legacy_names_) {
      return decapitalize(app_prefix_ + in);
    } else {
      return underscore(app_prefix_) + underscore(in);
    }
  }

  std::string atomify(std::string in) {
    if (legacy_names_) {
      return "'" + decapitalize(in) + "'";
    } else {
      return "'" + in + "'";
    }
  }

  std::string constify(std::string in) {
    if (legacy_names_) {
      return capitalize(in);
    } else {
      return uppercase(in);
    }
  }

  static std::string comment(string in);

private:
  bool has_default_value(t_field*);

  /* if true retain pre 0.9.2 naming scheme for functions, atoms and consts */
  bool legacy_names_;

  /* if true use maps instead of dicts in generated code */
  bool maps_;

  /* delimiter between namespace and record name */
  std::string delimiter_;

  /* used to avoid module name clashes for different applications */
  std::string app_prefix_;

  /* string type definition strategy */
  StringTo string_to_;

  /* sets type definition strategy */
  SetsTo sets_to_;

  /**
   * add function to export list
   */

  void export_function(t_function* tfunction, std::string prefix = "");
  void export_string(std::string name, int num);

  void export_types_string(std::string name, int num);

  /**
   * write out headers and footers for hrl files
   */

  void hrl_header(std::ostream& out, std::string name);
  void hrl_footer(std::ostream& out, std::string name);

  /**
   * stuff to spit out at the top of generated files
   */

  bool export_lines_first_;
  std::ostringstream export_lines_;

  bool export_types_lines_first_;
  std::ostringstream export_types_lines_;

  /**
   * File streams
   */

  std::ostringstream f_info_;
  std::ostringstream f_info_ext_;

  ofstream_with_content_based_conditional_update f_types_file_;
  ofstream_with_content_based_conditional_update f_types_hrl_file_;

  ofstream_with_content_based_conditional_update f_consts_file_;
  ofstream_with_content_based_conditional_update f_consts_hrl_file_;

  std::ostringstream f_service_;
  ofstream_with_content_based_conditional_update f_service_file_;
  ofstream_with_content_based_conditional_update f_service_hrl_;

  /**
   * Metadata containers
   */
  std::vector<std::string> v_struct_names_;
  std::vector<std::string> v_enum_names_;
  std::vector<std::string> v_exception_names_;
  std::vector<t_enum*> v_enums_;
  std::vector<t_const*> v_consts_;
};

/**
 * UI for file generation by opening up the necessary file output
 * streams.
 *
 * @param tprogram The program to generate
 */
void t_erl_generator::init_generator() {
  // Make output directory
  MKDIR(get_out_dir().c_str());

  // setup export lines
  export_lines_first_ = true;
  export_types_lines_first_ = true;

  string program_module_name = make_safe_for_module_name(program_name_);

  // types files
  string f_types_name = get_out_dir() + program_module_name + "_types.erl";
  string f_types_hrl_name = get_out_dir() + program_module_name + "_types.hrl";

  f_types_file_.open(f_types_name.c_str());
  f_types_hrl_file_.open(f_types_hrl_name.c_str());

  hrl_header(f_types_hrl_file_, program_module_name + "_types");

  f_types_file_ << erl_autogen_comment() << '\n'
                << "-module(" << program_module_name << "_types)." << '\n'
                << erl_imports() << '\n';

  f_types_file_ << "-include(\"" << program_module_name << "_types.hrl\")." << '\n'
                  << '\n';

  f_types_hrl_file_ << render_includes() << '\n';

  // consts files
  string f_consts_name = get_out_dir() + program_module_name + "_constants.erl";
  string f_consts_hrl_name = get_out_dir() + program_module_name + "_constants.hrl";

  f_consts_file_.open(f_consts_name.c_str());
  f_consts_hrl_file_.open(f_consts_hrl_name.c_str());

  f_consts_file_ << erl_autogen_comment() << '\n'
                 << "-module(" << program_module_name << "_constants)." << '\n'
                 << erl_imports() << '\n'
                 << "-include(\"" << program_module_name << "_types.hrl\")." << '\n'
                 << '\n';

  f_consts_hrl_file_ << erl_autogen_comment() << '\n' << erl_imports() << '\n'
                     << "-include(\"" << program_module_name << "_types.hrl\")." << '\n' << '\n';
}

/**
 * Boilerplate at beginning and end of header files
 */
void t_erl_generator::hrl_header(ostream& out, string name) {
  out << erl_autogen_comment() << '\n'
      << "-ifndef(_" << name << "_included)." << '\n' << "-define(_" << name << "_included, yeah)."
      << '\n';
}

void t_erl_generator::hrl_footer(ostream& out, string name) {
  (void)name;
  out << "-endif." << '\n';
}

/**
 * Renders all the imports necessary for including another Thrift program
 */
string t_erl_generator::render_includes() {
  const vector<t_program*>& includes = program_->get_includes();
  string result = "";
  for (auto include : includes) {
    result += "-include(\"" + make_safe_for_module_name(include->get_name())
              + "_types.hrl\").\n";
  }
  if (includes.size() > 0) {
    result += "\n";
  }
  return result;
}

/**
 * Autogen'd comment
 */
string t_erl_generator::erl_autogen_comment() {
  return std::string("%%\n") + "%% Autogenerated by Thrift Compiler (" + THRIFT_VERSION + ")\n"
         + "%%\n" + "%% DO NOT EDIT UNLESS YOU ARE SURE THAT YOU KNOW WHAT YOU ARE DOING\n"
         + "%%\n";
}

/**
 * Comment out text
 */

string t_erl_generator::comment(string in) {
  size_t pos = 0;
  in.insert(pos, "%% ");
  while ((pos = in.find_first_of('\n', pos)) != string::npos) {
    in.insert(++pos, "%% ");
  }
  return in;
}

/**
 * Prints standard thrift imports
 */
string t_erl_generator::erl_imports() {
  return "";
}

/**
 * Closes the type files
 */
void t_erl_generator::close_generator() {

  export_types_string("struct_info", 1);
  export_types_string("struct_info_ext", 1);
  export_types_string("enum_info", 1);
  export_types_string("enum_names", 0);
  export_types_string("struct_names", 0);
  export_types_string("exception_names", 0);

  f_types_file_ << "-export([" << export_types_lines_.str() << "])." << '\n' << '\n';

  f_types_file_ << f_info_.str();
  f_types_file_ << "struct_info(_) -> erlang:error(function_clause)." << '\n' << '\n';

  f_types_file_ << f_info_ext_.str();
  f_types_file_ << "struct_info_ext(_) -> erlang:error(function_clause)." << '\n' << '\n';

  generate_const_functions();

  generate_type_metadata("struct_names", v_struct_names_);
  generate_enum_metadata();
  generate_type_metadata("enum_names", v_enum_names_);
  generate_type_metadata("exception_names", v_exception_names_);

  hrl_footer(f_types_hrl_file_, string("BOGUS"));

  f_types_file_.close();
  f_types_hrl_file_.close();
  f_consts_file_.close();
  f_consts_hrl_file_.close();
}

const std::string emit_double_as_string(const double value) {
  std::stringstream double_output_stream;
  // sets the maximum precision: http://en.cppreference.com/w/cpp/io/manip/setprecision
  // sets the output format to fixed: http://en.cppreference.com/w/cpp/io/manip/fixed (not in scientific notation)
  double_output_stream << std::setprecision(std::numeric_limits<double>::digits10 + 1);

  #ifdef _MSC_VER
      // strtod is broken in MSVC compilers older than 2015, so std::fixed fails to format a double literal.
      // more details: https://blogs.msdn.microsoft.com/vcblog/2014/06/18/
      //               c-runtime-crt-features-fixes-and-breaking-changes-in-visual-studio-14-ctp1/
      //               and
      //               http://www.exploringbinary.com/visual-c-plus-plus-strtod-still-broken/
      #if _MSC_VER >= MSC_2015_VER
          double_output_stream << std::fixed;
      #else
          // note that if this function is called from the erlang generator and the MSVC compiler is older than 2015,
          // the double literal must be output in the scientific format. There can be some cases where the
          // mantissa of the output does not have fractionals, which is illegal in Erlang.
          // example => 10000000000000000.0 being output as 1e+16
          double_output_stream << std::scientific;
      #endif
  #else
      double_output_stream << std::fixed;
  #endif

  double_output_stream << value;

  return double_output_stream.str();
}

void t_erl_generator::generate_type_metadata(std::string function_name, vector<string> names) {
  size_t num_structs = names.size();

  indent(f_types_file_) << function_name << "() ->\n";
  indent_up();
  indent(f_types_file_) << "[";


  for(size_t i=0; i < num_structs; i++) {
    f_types_file_ << names.at(i);

    if (i < num_structs - 1) {
      f_types_file_ << ", ";
    }
  }

  f_types_file_ << "].\n\n";
  indent_down();
}

/**
 * Generates a typedef. no op
 *
 * @param ttypedef The type definition
 */
void t_erl_generator::generate_typedef(t_typedef* ttypedef) {
  (void)ttypedef;
}


void t_erl_generator::generate_const_function(t_const* tconst, ostringstream& exports, ostringstream& functions) {
  t_type* type = get_true_type(tconst->get_type());
  string name = tconst->get_name();
  t_const_value* value = tconst->get_value();

  if (type->is_map()) {
    t_type* ktype = ((t_map*)type)->get_key_type();
    t_type* vtype = ((t_map*)type)->get_val_type();
    string const_fun_name = lowercase(name);

    // Emit const function export.
    if (exports.tellp() > 0) { exports << ", "; }
    exports << const_fun_name << "/1, " << const_fun_name << "/2";

    // Emit const function definition.
    map<t_const_value*, t_const_value*, t_const_value::value_compare>::const_iterator i, end = value->get_map().end();
    // The one-argument form throws an error if the key does not exist in the map.
    for (i = value->get_map().begin(); i != end;) {
      functions << const_fun_name << "(" << render_const_value(ktype, i->first) << ") -> "
                << render_const_value(vtype, i->second);
      ++i;
      functions << (i != end ? ";\n" : ".\n\n");
    }

    // The two-argument form returns a default value if the key does not exist in the map.
    for (i = value->get_map().begin(); i != end; ++i) {
      functions << const_fun_name << "(" << render_const_value(ktype, i->first) << ", _) -> "
                << render_const_value(vtype, i->second) << ";\n";
    }
    functions << const_fun_name << "(_, Default) -> Default.\n\n";
  } else if (type->is_list()) {
    string const_fun_name = lowercase(name);

    if (exports.tellp() > 0) { exports << ", "; }
    exports << const_fun_name << "/1, " << const_fun_name << "/2";

    size_t list_size = value->get_list().size();
    string rendered_list = render_const_list_values(type, value);
    functions << const_fun_name << "(N) when N >= 1, N =< " << list_size << " ->\n"
              << indent_str() << "element(N, {" << rendered_list << "}).\n";
    functions << const_fun_name << "(N, _) when N >= 1, N =< " << list_size << " ->\n"
              << indent_str() << "element(N, {" << rendered_list << "});\n"
              << const_fun_name << "(_, Default) -> Default.\n\n";
    indent_down();
  }
}

void t_erl_generator::generate_const_functions() {
  ostringstream exports;
  ostringstream functions;
  vector<t_const*>::iterator c_iter;
  for (c_iter = v_consts_.begin(); c_iter != v_consts_.end(); ++c_iter) {
    generate_const_function(*c_iter, exports, functions);
  }
  if (exports.tellp() > 0) {
    f_consts_file_ << "-export([" << exports.str() << "]).\n\n"
                   << functions.str();
  }
}


/**
 * Generates code for an enumerated type. Done using a class to scope
 * the values.
 *
 * @param tenum The enumeration
 */
void t_erl_generator::generate_enum(t_enum* tenum) {
  vector<t_enum_value*> constants = tenum->get_constants();
  vector<t_enum_value*>::iterator c_iter;

  v_enums_.push_back(tenum);
  v_enum_names_.push_back(atomify(tenum->get_name()));

  for (c_iter = constants.begin(); c_iter != constants.end(); ++c_iter) {
    int value = (*c_iter)->get_value();
    string name = (*c_iter)->get_name();
    indent(f_types_hrl_file_) << "-define(" << constify(make_safe_for_module_name(program_name_))
                              << "_" << constify(tenum->get_name()) << "_" << constify(name) << ", "
                              << value << ")." << '\n';
  }

  f_types_hrl_file_ << '\n';
}

void t_erl_generator::generate_enum_info(t_enum* tenum){
  vector<t_enum_value*> constants = tenum->get_constants();
  size_t num_constants = constants.size();

  indent(f_types_file_) << "enum_info(" << atomify(tenum->get_name()) << ") ->\n";
  indent_up();
  indent(f_types_file_) << "[\n";

  for(size_t i=0; i < num_constants; i++) {
    indent_up();
    t_enum_value* value = constants.at(i);
    indent(f_types_file_) << "{" << atomify(value->get_name()) << ", " << value->get_value() << "}";

    if (i < num_constants - 1) {
      f_types_file_ << ",\n";
    }
    indent_down();
  }
  f_types_file_ << '\n';
  indent(f_types_file_) << "];\n\n";
  indent_down();
}

void t_erl_generator::generate_enum_metadata() {
  size_t enum_count = v_enums_.size();

  for(size_t i=0; i < enum_count; i++) {
    t_enum* tenum = v_enums_.at(i);
    generate_enum_info(tenum);
  }

  indent(f_types_file_) << "enum_info(_) -> erlang:error(function_clause).\n\n";
}

/**
 * Generate a constant value
 */
void t_erl_generator::generate_const(t_const* tconst) {
  t_type* type = tconst->get_type();
  string name = tconst->get_name();
  t_const_value* value = tconst->get_value();

  // Save the tconst so that function can be emitted in generate_const_functions().
  v_consts_.push_back(tconst);

  f_consts_hrl_file_ << "-define(" << constify(make_safe_for_module_name(program_name_)) << "_"
                     << constify(name) << ", " << render_const_value(type, value) << ")." << '\n' << '\n';
}

/**
 * Prints the value of a constant with the given type. Note that type checking
 * is NOT performed in this function as it is always run beforehand using the
 * validate_types method in main.cc
 */
string t_erl_generator::render_const_value(t_type* type, t_const_value* value) {
  type = get_true_type(type);
  std::ostringstream out;

  if (type->is_base_type()) {
    t_base_type::t_base tbase = ((t_base_type*)type)->get_base();
    switch (tbase) {
    case t_base_type::TYPE_STRING:
      out << render_const_string_value(value);
      break;
    case t_base_type::TYPE_BOOL:
      out << (value->get_integer() > 0 ? "true" : "false");
      break;
    case t_base_type::TYPE_I8:
    case t_base_type::TYPE_I16:
    case t_base_type::TYPE_I32:
    case t_base_type::TYPE_I64:
      out << value->get_integer();
      break;
    case t_base_type::TYPE_DOUBLE:
      if (value->get_type() == t_const_value::CV_INTEGER) {
        out << "float(" << value->get_integer() << ")";
      } else {
        out << emit_double_as_string(value->get_double());
      }
      break;
    default:
      throw "compiler error: no const of base type " + t_base_type::t_base_name(tbase);
    }
  } else if (type->is_enum()) {
    indent(out) << value->get_integer();

  } else if (type->is_struct() || type->is_xception()) {
    out << "#" << type_name(type) << "{";
    const vector<t_field*>& fields = ((t_struct*)type)->get_members();
    vector<t_field*>::const_iterator f_iter;
    const map<t_const_value*, t_const_value*, t_const_value::value_compare>& val = value->get_map();
    map<t_const_value*, t_const_value*, t_const_value::value_compare>::const_iterator v_iter;

    bool first = true;
    for (v_iter = val.begin(); v_iter != val.end(); ++v_iter) {
      t_type* field_type = nullptr;
      for (f_iter = fields.begin(); f_iter != fields.end(); ++f_iter) {
        if ((*f_iter)->get_name() == v_iter->first->get_string()) {
          field_type = (*f_iter)->get_type();
        }
      }
      if (field_type == nullptr) {
        throw "type error: " + type->get_name() + " has no field " + v_iter->first->get_string();
      }

      if (first) {
        first = false;
      } else {
        out << ",";
      }
      out << v_iter->first->get_string();
      out << " = ";
      out << render_const_value(field_type, v_iter->second);
    }
    indent_down();
    indent(out) << "}";

  } else if (type->is_map()) {
    t_type* ktype = ((t_map*)type)->get_key_type();
    t_type* vtype = ((t_map*)type)->get_val_type();

    if (maps_) {
      out << "maps:from_list([";
    } else {
      out << "dict:from_list([";
    }
    map<t_const_value*, t_const_value*, t_const_value::value_compare>::const_iterator i, end = value->get_map().end();
    for (i = value->get_map().begin(); i != end;) {
      out << "{" << render_const_value(ktype, i->first) << ","
          << render_const_value(vtype, i->second) << "}";
      if (++i != end) {
        out << ",";
      }
    }
    out << "])";
  } else if (type->is_set()) {
    t_type* etype = ((t_set*)type)->get_elem_type();
    out << "sets:from_list([";
    vector<t_const_value*>::const_iterator i, end = value->get_list().end();
    for (i = value->get_list().begin(); i != end;) {
      out << render_const_value(etype, *i);
      if (++i != end) {
        out << ",";
      }
    }
    out << "]";
    if (sets_to_ == SetsTo::V2) {
      out << ", [{version, 2}]";
    }
    out << ")";
  } else if (type->is_list()) {
    out << "[" << render_const_list_values(type, value) << "]";
  } else {
    throw "CANNOT GENERATE CONSTANT FOR TYPE: " + type->get_name();
  }
  return out.str();
}

string t_erl_generator::render_const_list_values(t_type* type, t_const_value* value) {
  std::ostringstream out;
  t_type* etype = ((t_list*)type)->get_elem_type();

  bool first = true;
  const vector<t_const_value*>& val = value->get_list();
  vector<t_const_value*>::const_iterator v_iter;
  for (v_iter = val.begin(); v_iter != val.end(); ++v_iter) {
    if (first) {
      first = false;
    } else {
      out << ",";
    }
    out << render_const_value(etype, *v_iter);
  }
  return out.str();
}

string t_erl_generator::render_const_string_value(t_const_value* constval) {
  if (string_to_ == StringTo::Binary) {
    return "<<\"" + get_escaped_string(constval) + "\">>";
  }
  return '"' + get_escaped_string(constval) + '"';
}


string t_erl_generator::render_default_value(t_field* field) {
  t_type* type = field->get_type();
  if (type->is_struct() || type->is_xception()) {
    return "#" + type_name(type) + "{}";
  } else if (type->is_map()) {
    if (maps_) {
      return "#{}";
    } else {
      return "dict:new()";
    }
  } else if (type->is_set()) {
    return render_default_sets_value();
  } else if (type->is_list()) {
    return "[]";
  } else {
    return "undefined";
  }
}

string t_erl_generator::render_default_sets_value() {
  switch (sets_to_) {
  case SetsTo::V1:
    return "sets:new()";
  case SetsTo::V2:
    return "sets:new([{version,2}])";
  default:
    throw "compiler error: unsupported set type";
  }
}

string t_erl_generator::render_member_type(t_field* field) {
  t_type* type = get_true_type(field->get_type());
  if (type->is_base_type()) {
    t_base_type::t_base tbase = ((t_base_type*)type)->get_base();
    switch (tbase) {
    case t_base_type::TYPE_STRING:
      return render_string_type();
    case t_base_type::TYPE_BOOL:
      return "boolean()";
    case t_base_type::TYPE_I8:
    case t_base_type::TYPE_I16:
    case t_base_type::TYPE_I32:
    case t_base_type::TYPE_I64:
      return "integer()";
    case t_base_type::TYPE_DOUBLE:
      return "float()";
    default:
      throw "compiler error: unsupported base type " + t_base_type::t_base_name(tbase);
    }
  } else if (type->is_enum()) {
    return "integer()";
  } else if (type->is_struct() || type->is_xception()) {
    return type_name(type) + "()";
  } else if (type->is_map()) {
    if (maps_) {
      return "map()";
    } else {
      return "dict:dict()";
    }
  } else if (type->is_set()) {
      return "sets:set()";
  } else if (type->is_list()) {
    return "list()";
  } else {
    throw "compiler error: unsupported type " + type->get_name();
  }
}

string t_erl_generator::render_string_type() {
  switch (string_to_) {
  case StringTo::String:
    return "string()";
  case StringTo::Binary:
    return "binary()";
  case StringTo::Both:
    return "string() | binary()";
  default:
    throw "compiler error: unsupported string type";
  }
}

string t_erl_generator::render_member_requiredness(t_field* field) {
  switch (field->get_req()) {
  case t_field::T_REQUIRED:
    return "required";
  case t_field::T_OPTIONAL:
    return "optional";
  default:
    return "undefined";
  }
}

/**
 * Generates a struct
 */
void t_erl_generator::generate_struct(t_struct* tstruct) {
  v_struct_names_.push_back(type_name(tstruct));
  generate_erl_struct(tstruct, false);
}

/**
 * Generates a struct definition for a thrift exception. Basically the same
 * as a struct but extends the Exception class.
 *
 * @param txception The struct definition
 */
void t_erl_generator::generate_xception(t_struct* txception) {
  v_exception_names_.push_back(type_name(txception));
  generate_erl_struct(txception, true);
}

/**
 * Generates a struct
 */
void t_erl_generator::generate_erl_struct(t_struct* tstruct, bool is_exception) {
  (void)is_exception;
  generate_erl_struct_definition(f_types_hrl_file_, tstruct);
  generate_erl_struct_info(f_info_, tstruct);
  generate_erl_extended_struct_info(f_info_ext_, tstruct);
}

/**
 * Generates a struct definition for a thrift data type.
 *
 * @param tstruct The struct definition
 */
void t_erl_generator::generate_erl_struct_definition(ostream& out, t_struct* tstruct) {
  indent(out) << "%% ";
  if (tstruct->is_union()) {
    out << "union ";
  } else {
    out << "struct ";
  }
  out << type_name(tstruct) << '\n' << '\n';

  std::stringstream buf;
  buf << indent() << "-record(" << type_name(tstruct) << ", {";
  string field_indent(buf.str().size(), ' ');

  const vector<t_field*>& members = tstruct->get_members();
  for (vector<t_field*>::const_iterator m_iter = members.begin(); m_iter != members.end();) {
    generate_erl_struct_member(buf, *m_iter);
    if (++m_iter != members.end()) {
      buf << "," << '\n' << field_indent;
    }
  }
  buf << "}).";

  out << buf.str() << '\n';
  out << "-type " + type_name(tstruct) << "() :: #" + type_name(tstruct) + "{}." << '\n' << '\n';
}

/**
 * Generates the record field definition
 */

void t_erl_generator::generate_erl_struct_member(ostream& out, t_field* tmember) {
  out << atomify(tmember->get_name());
  if (has_default_value(tmember))
    out << " = " << render_member_value(tmember);
  out << " :: " << render_member_type(tmember);
  if (tmember->get_req() != t_field::T_REQUIRED)
    out << " | 'undefined'";
}

bool t_erl_generator::has_default_value(t_field* field) {
  t_type* type = field->get_type();
  if (!field->get_value()) {
    if (field->get_req() == t_field::T_REQUIRED) {
      if (type->is_struct() || type->is_xception() || type->is_map() || type->is_set()
          || type->is_list()) {
        return true;
      } else {
        return false;
      }
    } else {
      return false;
    }
  } else {
    return true;
  }
}

string t_erl_generator::render_member_value(t_field* field) {
  if (!field->get_value()) {
    return render_default_value(field);
  } else {
    return render_const_value(field->get_type(), field->get_value());
  }
}

/**
 * Generates the read method for a struct
 */
void t_erl_generator::generate_erl_struct_info(ostream& out, t_struct* tstruct) {
  indent(out) << "struct_info(" << type_name(tstruct) << ") ->" << '\n';
  indent_up();
  out << indent() << render_type_term(tstruct, true) << ";" << '\n';
  indent_down();
  out << '\n';
}

void t_erl_generator::generate_erl_extended_struct_info(ostream& out, t_struct* tstruct) {
  indent(out) << "struct_info_ext(" << type_name(tstruct) << ") ->" << '\n';
  indent_up();
  out << indent() << render_type_term(tstruct, true, true) << ";" << '\n';
  indent_down();
  out << '\n';
}

/**
 * Generates a thrift service.
 *
 * @param tservice The service definition
 */
void t_erl_generator::generate_service(t_service* tservice) {
  service_name_ = make_safe_for_module_name(service_name_);

  string f_service_hrl_name = get_out_dir() + service_name_ + "_thrift.hrl";
  string f_service_name = get_out_dir() + service_name_ + "_thrift.erl";
  f_service_file_.open(f_service_name.c_str());
  f_service_hrl_.open(f_service_hrl_name.c_str());

  // Reset service text aggregating stream streams
  f_service_.str("");
  export_lines_.str("");
  export_lines_first_ = true;

  hrl_header(f_service_hrl_, service_name_);

  if (tservice->get_extends() != nullptr) {
    f_service_hrl_ << "-include(\""
                   << make_safe_for_module_name(tservice->get_extends()->get_name())
                   << "_thrift.hrl\"). % inherit " << '\n';
  }

  f_service_hrl_ << "-include(\"" << make_safe_for_module_name(program_name_) << "_types.hrl\")."
                 << '\n' << '\n';

  // Generate the three main parts of the service (well, two for now in PHP)
  generate_service_helpers(tservice); // cpiro: New Erlang Order

  generate_service_interface(tservice);

  generate_service_metadata(tservice);

  // indent_down();

  f_service_file_ << erl_autogen_comment() << '\n' << "-module(" << service_name_ << "_thrift)."
                  << '\n' << "-behaviour(thrift_service)." << '\n' << '\n' << erl_imports() << '\n';

  f_service_file_ << "-include(\"" << make_safe_for_module_name(tservice->get_name())
                  << "_thrift.hrl\")." << '\n' << '\n';

  f_service_file_ << "-export([" << export_lines_.str() << "])." << '\n' << '\n';

  f_service_file_ << f_service_.str();

  hrl_footer(f_service_hrl_, f_service_name);

  // Close service file
  f_service_file_.close();
  f_service_hrl_.close();
}

void t_erl_generator::generate_service_metadata(t_service* tservice) {
  export_string("function_names", 0);
  vector<t_function*> functions = tservice->get_functions();
  size_t num_functions = functions.size();

  indent(f_service_) << "function_names() -> " << '\n';
  indent_up();
  indent(f_service_) << "[";

  for (size_t i=0; i < num_functions; i++) {
    t_function* current = functions.at(i);
    f_service_ << atomify(current->get_name());
    if (i < num_functions - 1) {
      f_service_ << ", ";
    }
  }

  f_service_ << "].\n\n";
  indent_down();
}

/**
 * Generates helper functions for a service.
 *
 * @param tservice The service to generate a header definition for
 */
void t_erl_generator::generate_service_helpers(t_service* tservice) {
  vector<t_function*> functions = tservice->get_functions();
  vector<t_function*>::iterator f_iter;

  //  indent(f_service_) <<
  //  "% HELPER FUNCTIONS AND STRUCTURES" << '\n' << '\n';

  export_string("struct_info", 1);

  for (f_iter = functions.begin(); f_iter != functions.end(); ++f_iter) {
    generate_erl_function_helpers(*f_iter);
  }
  f_service_ << "struct_info(_) -> erlang:error(function_clause)." << '\n';
}

/**
 * Generates a struct and helpers for a function.
 *
 * @param tfunction The function
 */
void t_erl_generator::generate_erl_function_helpers(t_function* tfunction) {
  (void)tfunction;
}

/**
 * Generates a service interface definition.
 *
 * @param tservice The service to generate a header definition for
 */
void t_erl_generator::generate_service_interface(t_service* tservice) {

  export_string("function_info", 2);

  vector<t_function*> functions = tservice->get_functions();
  vector<t_function*>::iterator f_iter;
  f_service_ << "%%% interface" << '\n';
  for (f_iter = functions.begin(); f_iter != functions.end(); ++f_iter) {
    f_service_ << indent() << "% " << function_signature(*f_iter) << '\n';

    generate_function_info(tservice, *f_iter);
  }

  // Inheritance - pass unknown functions to base class
  if (tservice->get_extends() != nullptr) {
    indent(f_service_) << "function_info(Function, InfoType) ->" << '\n';
    indent_up();
    indent(f_service_) << make_safe_for_module_name(tservice->get_extends()->get_name())
                       << "_thrift:function_info(Function, InfoType)." << '\n';
    indent_down();
  } else {
    // return function_clause error for non-existent functions
    indent(f_service_) << "function_info(_Func, _Info) -> erlang:error(function_clause)." << '\n';
  }

  indent(f_service_) << '\n';
}

/**
 * Generates a function_info(FunctionName, params_type) and
 * function_info(FunctionName, reply_type)
 */
void t_erl_generator::generate_function_info(t_service* tservice, t_function* tfunction) {
  (void)tservice;
  string name_atom = atomify(tfunction->get_name());

  t_struct* xs = tfunction->get_xceptions();
  t_struct* arg_struct = tfunction->get_arglist();

  // function_info(Function, params_type):
  indent(f_service_) << "function_info(" << name_atom << ", params_type) ->" << '\n';
  indent_up();

  indent(f_service_) << render_type_term(arg_struct, true) << ";" << '\n';

  indent_down();

  // function_info(Function, reply_type):
  indent(f_service_) << "function_info(" << name_atom << ", reply_type) ->" << '\n';
  indent_up();

  if (!tfunction->get_returntype()->is_void())
    indent(f_service_) << render_type_term(tfunction->get_returntype(), false) << ";" << '\n';
  else if (tfunction->is_oneway())
    indent(f_service_) << "oneway_void;" << '\n';
  else
    indent(f_service_) << "{struct, []}"
                       << ";" << '\n';
  indent_down();

  // function_info(Function, exceptions):
  indent(f_service_) << "function_info(" << name_atom << ", exceptions) ->" << '\n';
  indent_up();
  indent(f_service_) << render_type_term(xs, true) << ";" << '\n';
  indent_down();
}

/**
 * Renders a function signature of the form 'type name(args)'
 *
 * @param tfunction Function definition
 * @return String of rendered function definition
 */
string t_erl_generator::function_signature(t_function* tfunction, string prefix) {
  return prefix + tfunction->get_name() + "(This"
         + capitalize(argument_list(tfunction->get_arglist())) + ")";
}

/**
 * Add a function to the exports list
 */
void t_erl_generator::export_string(string name, int num) {
  if (export_lines_first_) {
    export_lines_first_ = false;
  } else {
    export_lines_ << ", ";
  }
  export_lines_ << name << "/" << num;
}

void t_erl_generator::export_types_string(string name, int num) {
  if (export_types_lines_first_) {
    export_types_lines_first_ = false;
  } else {
    export_types_lines_ << ", ";
  }
  export_types_lines_ << name << "/" << num;
}

void t_erl_generator::export_function(t_function* tfunction, string prefix) {
  t_struct::members_type::size_type num = tfunction->get_arglist()->get_members().size();
  if (num > static_cast<t_struct::members_type::size_type>(std::numeric_limits<int>().max())) {
    throw "integer overflow in t_erl_generator::export_function, name " + tfunction->get_name();
  }
  export_string(prefix + tfunction->get_name(),
                1 // This
                + static_cast<int>(num));
}

/**
 * Renders a field list
 */
string t_erl_generator::argument_list(t_struct* tstruct) {
  string result = "";

  const vector<t_field*>& fields = tstruct->get_members();
  vector<t_field*>::const_iterator f_iter;
  bool first = true;
  for (f_iter = fields.begin(); f_iter != fields.end(); ++f_iter) {
    if (first) {
      first = false;
      result += ", "; // initial comma to compensate for initial This
    } else {
      result += ", ";
    }
    result += capitalize((*f_iter)->get_name());
  }
  return result;
}

string t_erl_generator::type_name(t_type* ttype) {
  string prefix = ttype->get_program()->get_namespace("erl");
  size_t prefix_length = prefix.length();
  if (prefix_length > 0 && prefix[prefix_length - 1] != '_') {
    size_t delimiter_length = delimiter_.length();
    if (delimiter_length > 0 && delimiter_length < prefix_length) {
        bool not_match = prefix.compare(prefix_length - delimiter_length, prefix_length, delimiter_) != 0;
        if (not_match) {
            prefix += delimiter_;
        }
    }
  }

  string name = ttype->get_name();

  return atomify(prefix + name);
}

/**
 * Converts the parse type to a Erlang "type" (macro for int constants)
 */
string t_erl_generator::type_to_enum(t_type* type) {
  type = get_true_type(type);

  if (type->is_base_type()) {
    t_base_type::t_base tbase = ((t_base_type*)type)->get_base();
    switch (tbase) {
    case t_base_type::TYPE_VOID:
      throw "NO T_VOID CONSTRUCT";
    case t_base_type::TYPE_STRING:
      return "?tType_STRING";
    case t_base_type::TYPE_BOOL:
      return "?tType_BOOL";
    case t_base_type::TYPE_I8:
      return "?tType_I8";
    case t_base_type::TYPE_I16:
      return "?tType_I16";
    case t_base_type::TYPE_I32:
      return "?tType_I32";
    case t_base_type::TYPE_I64:
      return "?tType_I64";
    case t_base_type::TYPE_DOUBLE:
      return "?tType_DOUBLE";
    default:
      break;
    }
  } else if (type->is_enum()) {
    return "?tType_I32";
  } else if (type->is_struct() || type->is_xception()) {
    return "?tType_STRUCT";
  } else if (type->is_map()) {
    return "?tType_MAP";
  } else if (type->is_set()) {
    return "?tType_SET";
  } else if (type->is_list()) {
    return "?tType_LIST";
  }

  throw "INVALID TYPE IN type_to_enum: " + type->get_name();
}

/**
 * Generate an Erlang term which represents a thrift type
 */
std::string t_erl_generator::render_type_term(t_type* type,
                                              bool expand_structs,
                                              bool extended_info) {
  type = get_true_type(type);

  if (type->is_base_type()) {
    t_base_type::t_base tbase = ((t_base_type*)type)->get_base();
    switch (tbase) {
    case t_base_type::TYPE_VOID:
      throw "NO T_VOID CONSTRUCT";
    case t_base_type::TYPE_STRING:
      return "string";
    case t_base_type::TYPE_BOOL:
      return "bool";
    case t_base_type::TYPE_I8:
      return "byte";
    case t_base_type::TYPE_I16:
      return "i16";
    case t_base_type::TYPE_I32:
      return "i32";
    case t_base_type::TYPE_I64:
      return "i64";
    case t_base_type::TYPE_DOUBLE:
      return "double";
    default:
      break;
    }
  } else if (type->is_enum()) {
    return "i32";
  } else if (type->is_struct() || type->is_xception()) {
    if (expand_structs) {

      std::stringstream buf;
      buf << "{struct, [";
      string field_indent(buf.str().size(), ' ');

      t_struct::members_type const& fields = static_cast<t_struct*>(type)->get_members();
      t_struct::members_type::const_iterator i, end = fields.end();
      for (i = fields.begin(); i != end;) {
        t_struct::members_type::value_type member = *i;
        int32_t key = member->get_key();
        string type = render_type_term(member->get_type(), false, false); // recursive call

        if (!extended_info) {
          // Convert to format: {struct, [{Fid, Type}|...]}
          buf << "{" << key << ", " << type << "}";
        } else {
          // Convert to format: {struct, [{Fid, Req, Type, Name, Def}|...]}
          string name = member->get_name();
          string value = render_member_value(member);
          string requiredness = render_member_requiredness(member);
          buf << "{" << key << ", " << requiredness << ", " << type << ", " << atomify(name) << ", "
              << value << "}";
        }

        if (++i != end) {
          buf << "," << '\n' << field_indent;
        }
      }

      buf << "]}" << '\n';
      return buf.str();
    } else {
      return "{struct, {" + atomify(type_module(type)) + ", " + type_name(type) + "}}";
    }
  } else if (type->is_map()) {
    // {map, KeyType, ValType}
    t_type* key_type = ((t_map*)type)->get_key_type();
    t_type* val_type = ((t_map*)type)->get_val_type();

    return "{map, " + render_type_term(key_type, false) + ", " + render_type_term(val_type, false)
           + "}";

  } else if (type->is_set()) {
    t_type* elem_type = ((t_set*)type)->get_elem_type();

    return "{set, " + render_type_term(elem_type, false) + "}";

  } else if (type->is_list()) {
    t_type* elem_type = ((t_list*)type)->get_elem_type();

    return "{list, " + render_type_term(elem_type, false) + "}";
  }

  throw "INVALID TYPE IN type_to_enum: " + type->get_name();
}

std::string t_erl_generator::type_module(t_type* ttype) {
  return make_safe_for_module_name(ttype->get_program()->get_name()) + "_types";
}

std::string t_erl_generator::display_name() const {
  return "Erlang";
}


THRIFT_REGISTER_GENERATOR(
    erl,
    "Erlang",
    "    legacynames:     Output files retain naming conventions of Thrift 0.9.1 and earlier.\n"
    "    delimiter=       Delimiter between namespace prefix and record name. Default is '.'.\n"
    "    app_prefix=      Application prefix for generated Erlang files.\n"
    "    maps:            Generate maps instead of dicts.\n"
    "    string=          Define string as 'string', 'binary' or 'both'. Default is 'both'.\n"
    "    set=             Define sets implementation, supported 'v1' and 'v2'. Default is 'v1'.\n")
