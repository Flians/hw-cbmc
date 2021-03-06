/*******************************************************************\

Module: Verilog Type Checker Base

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <cassert>

#include <util/ebmc_util.h>
#include <util/expr_util.h>
#include <util/prefix.h>
#include <util/std_types.h>

#include "expr2verilog.h"
#include "verilog_typecheck_base.h"

/*******************************************************************\

Function: verilog_module_symbol

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

irep_idt verilog_module_symbol(const irep_idt &base_name)
{
  return "Verilog::"+id2string(base_name);
}

/*******************************************************************\

Function: strip_verilog_prefix

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

irep_idt strip_verilog_prefix(const irep_idt &identifier)
{
  std::string prefix="Verilog::";
  assert(has_prefix(id2string(identifier), prefix));
  assert(identifier.size()>=prefix.size());
  return identifier.c_str()+prefix.size();
}

/*******************************************************************\

Function: verilog_module_name

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

irep_idt verilog_module_name(const irep_idt &identifier)
{
  return strip_verilog_prefix(identifier);
}

/*******************************************************************\

Function: verilog_typecheck_baset::to_string

  Inputs: Type

 Outputs:

 Purpose:

\*******************************************************************/

std::string verilog_typecheck_baset::to_string(const typet &type)
{
  return type2verilog(type);
}

/*******************************************************************\

Function: verilog_typecheck_baset::to_string

  Inputs: Expression

 Outputs: String representing the expression

 Purpose:

\*******************************************************************/

std::string verilog_typecheck_baset::to_string(const exprt &expr)
{
  return expr2verilog(expr);
}

/*******************************************************************\

Function: verilog_typecheck_baset::array_size

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

mp_integer verilog_typecheck_baset::array_size(const typet &type)
{
  mp_integer size;

  if(type.id()!=ID_array)
  {
    error() << "array_size expected array type" << eom;
    throw 0;
  }

  if(to_integer(to_array_type(type).size(), size))
  {
    error() << "failed to get array size of array type" << eom;
    throw 0;
  }

  return size;
}

/*******************************************************************\

Function: verilog_typecheck_baset::array_offset

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

mp_integer verilog_typecheck_baset::array_offset(const typet &type)
{
  mp_integer offset;

  if(to_integer(static_cast<const exprt &>(type.find(ID_offset)), offset))
  {
    error() << "failed to get array offset of type `"
            << type.id() << '\'' << eom;
    throw 0;
  }

  return offset;
}

/*******************************************************************\

Function: verilog_typecheck_baset::get_width

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::size_t verilog_typecheck_baset::get_width(const typet &type)
{
  if(type.id()==ID_bool)
    return 1;

  if(type.id()==ID_unsignedbv || type.id()==ID_signedbv ||
     type.id()==ID_verilog_signedbv || type.id()==ID_verilog_unsignedbv)
    return type.get_int(ID_width);

  if(type.id()==ID_array)
  {
    mp_integer subtype_width=get_width(type.subtype());
    return (array_size(type) * subtype_width).to_ulong();
  }
  
  if(type.id()==ID_integer)
  {
    // The standard says these are at least 32 bits wide.
    return 32;
  }

  error() << "type `" << type.id() << "' has unknown width"
          << eom;
  throw 0;
}

/*******************************************************************\

Function: verilog_typecheck_baset::index_type

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

typet verilog_typecheck_baset::index_type(const typet &array_type)
{
  return unsignedbv_typet(address_bits(
      (array_size(array_type) + array_offset(array_type)).to_ulong()));
}
