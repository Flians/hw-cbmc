/*******************************************************************\

Module: Base for Verification Modules

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <fstream>
#include <iostream>

#include <util/time_stopping.h>
#include <util/get_module.h>
#include <util/xml.h>
#include <util/find_macros.h>
#include <util/xml_irep.h>
#include <util/config.h>
#include <util/cmdline.h>
#include <util/string2int.h>
#include <util/expr_util.h>
#include <util/decision_procedure.h>
#include <util/unicode.h>

#include <trans-netlist/trans_trace_netlist.h>
#include <trans-netlist/ldg.h>
#include <trans-netlist/trans_to_netlist.h>
#include <trans-netlist/unwind_netlist.h>
#include <trans-netlist/compute_ct.h>

#include <trans-word-level/trans_trace_word_level.h>
#include <trans-word-level/property.h>
#include <trans-word-level/unwind.h>
#include <trans-word-level/show_modules.h>

#include <langapi/language_util.h>
#include <langapi/mode.h>
#include <langapi/languages.h>

#include "ebmc_base.h"
#include "ebmc_version.h"

/*******************************************************************\

Function: ebmc_baset::ebmc_baset

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

ebmc_baset::ebmc_baset(
  const cmdlinet &_cmdline,
  ui_message_handlert &_ui_message_handler):
  language_uit(_cmdline, _ui_message_handler),
  cmdline(_cmdline),
  main_symbol(NULL)
{
  if(cmdline.isset("verbosity"))
    ui_message_handler.set_verbosity(
      unsafe_string2unsigned(cmdline.get_value("verbosity")));
  else
    ui_message_handler.set_verbosity(messaget::M_STATUS); // default
}

/*******************************************************************\

Function: ebmc_baset::finish_bmc

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

int ebmc_baset::finish_bmc(prop_convt &solver)
{
  // convert the properties
  
  for(propertyt &property : properties)
  {
    if(property.is_disabled())
      continue;
    
    const namespacet ns(symbol_table);
    
    ::property(property.expr, property.timeframe_literals,
               get_message_handler(), solver, bound+1, ns);
               
    // freeze for incremental usage
    for(auto l : property.timeframe_literals)
      solver.set_frozen(l);
  }
  
  status() << "Solving with "
           << solver.decision_procedure_text() << eom;

  absolute_timet sat_start_time=current_time();
  
  // Use assumptions to check the properties separately
  
  for(propertyt &property : properties)
  {
    if(property.is_disabled())
      continue;
    
    status() << "Checking " << property.name << eom;
    
    or_exprt or_expr;
    
    for(auto l : property.timeframe_literals)
      or_expr.operands().push_back(literal_exprt(!l));
      
    literalt property_literal=solver.convert(or_expr);
    
    bvt assumptions;
    assumptions.push_back(property_literal);
    solver.set_assumptions(assumptions);

    decision_proceduret::resultt dec_result=
      solver.dec_solve();

    switch(dec_result)
    {
    case decision_proceduret::resultt::D_SATISFIABLE:
      {
        result() << "SAT: counterexample found" << eom;
        
        property.make_failure();

        namespacet ns(symbol_table);
    
        compute_trans_trace(
          property.timeframe_literals,
          solver,
          bound+1,
          ns,
          main_symbol->name,
          property.counterexample);
      }
      break;

    case decision_proceduret::resultt::D_UNSATISFIABLE:
      result() << "UNSAT: No counterexample found within bound" << eom;
      property.make_success();
      break;

    case decision_proceduret::resultt::D_ERROR:
      error() << "Error from decision procedure" << eom;
      return 2;

    default:
      error() << "Unexpected result from decision procedure" << eom;
      return 1;
    }
  }

  statistics() << "Solver time: " << (current_time()-sat_start_time)
               << eom;

  // We return '0' if the property holds,
  // and '10' if it is violated.
  return property_failure()?10:0; 
}

/*******************************************************************\

Function: ebmc_baset::finish_bmc

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

int ebmc_baset::finish_bmc(const bmc_mapt &bmc_map, propt &solver)
{
  // convert the properties
  for(propertyt &property : properties)
  {
    if(property.is_disabled())
      continue;
    
    const namespacet ns(symbol_table);
    
    ::unwind_property(property.expr, property.timeframe_literals,
                      get_message_handler(), solver, bmc_map, ns);

    // freeze for incremental usage
    for(auto l : property.timeframe_literals)
      solver.set_frozen(l);
  }
  
  absolute_timet sat_start_time=current_time();
  
  status() << "Solving with " << solver.solver_text() << eom;

  for(propertyt &property : properties)
  {
    if(property.is_disabled())
      continue;
    
    status() << "Checking " << property.name << eom;
  
    literalt property_literal=!solver.land(property.timeframe_literals);
  
    bvt assumptions;
    assumptions.push_back(property_literal);
    solver.set_assumptions(assumptions);
  
    propt::resultt prop_result=
      solver.prop_solve();
    
    switch(prop_result)
    {
    case propt::resultt::P_SATISFIABLE:
      {
        result() << "SAT: counterexample found" << eom;
        
        property.make_failure();

        namespacet ns(symbol_table);

        compute_trans_trace(
          property.timeframe_literals,
          bmc_map,
          solver,
          ns,
          property.counterexample);
      }
      break;

    case propt::resultt::P_UNSATISFIABLE:
      result() << "UNSAT: No counterexample found within bound" << eom;
      property.make_success();
      break;

    case propt::resultt::P_ERROR:
      error() << "Error from decision procedure" << eom;
      return 2;

    default:
      error() << "Unexpected result from decision procedure" << eom;
      return 1;
    }
  }
    
  statistics() << "Solver time: " << (current_time()-sat_start_time)
               << eom;

  // We return '0' if the property holds,
  // and '10' if it is violated.
  return property_failure()?10:0; 
}

/*******************************************************************\

Function: ebmc_baset::parse_property

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool ebmc_baset::parse_property(
  const std::string &property)
{
  namespacet ns(symbol_table);

  languaget* language=get_language_from_mode(main_symbol->mode);
  language->set_message_handler(get_message_handler());
  languagest languages(ns, language);

  exprt expr;
  if(languages.to_expr(
    property,
    id2string(main_symbol->module),
    expr))
    return true;

  // We give it an implict always, as in SVA
  
  if(expr.id()!=ID_sva_always)
  {
    unary_predicate_exprt tmp(ID_sva_always, expr);
    expr.swap(tmp);
  }

  std::string expr_as_string;
  languages.from_expr(expr, expr_as_string);
  debug() << "Property: " << expr_as_string << eom;
  debug() << "Mode: " << main_symbol->mode << eom;

  properties.push_back(propertyt());
  properties.back().expr=expr;
  properties.back().expr_string=expr_as_string;
  properties.back().mode=main_symbol->mode;
  properties.back().location.make_nil();
  properties.back().description="command-line assertion";
  properties.back().name="command-line assertion";
  
  return false;
}

/*******************************************************************\

Function: ebmc_baset::get_model_properties

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool ebmc_baset::get_model_properties()
{
  forall_symbol_module_map(
    it,
    symbol_table.symbol_module_map, 
    main_symbol->name)
  {
    namespacet ns(symbol_table);
    const symbolt &symbol=ns.lookup(it->second);

    if(symbol.is_property)
    {
      try
      {
        std::string value_as_string=
          from_expr(ns, symbol.name, symbol.value);

        debug() << "Property: " << value_as_string << eom;

        properties.push_back(propertyt());
        properties.back().number=properties.size()-1;

        if(symbol.pretty_name.empty())
          properties.back().name=symbol.name;
        else
          properties.back().name=symbol.pretty_name;

        properties.back().expr=symbol.value;
        properties.back().location=symbol.location;
        properties.back().expr_string=value_as_string;
        properties.back().mode=symbol.mode;
        properties.back().description=id2string(symbol.location.get_comment());
      }
      
      catch(const char *e)
      {
        error() << e << eom;
        return true;
      }
      
      catch(const std::string &e)
      {
        error() << e << eom;
        return true;
      }
      
      catch(int)
      {
        return true;
      }  

    }
  }
  
  if(cmdline.isset("property"))
  {
    std::string property=cmdline.get_value("property");

    for(auto & p : properties)
      p.status=propertyt::statust::DISABLED;
      
    bool found=false;

    for(auto & p : properties)
      if(p.name==property)
      {
        found=true;
        p.status=propertyt::statust::UNKNOWN;
        break;
      }
    
    if(!found)
    {
      error() << "Property " << property << " not found" << eom;
      return true;
    }
  }
  
  return false;
}

/*******************************************************************\

Function: ebmc_baset::get_bound

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool ebmc_baset::get_bound()
{
  if(!cmdline.isset("bound"))
  {
    warning() << "using default bound 1" << eom;
    bound=1;
    return false;
  }

  bound=unsafe_string2unsigned(cmdline.get_value("bound"));

  return false;
}

/*******************************************************************\

Function: ebmc_baset::get_main

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool ebmc_baset::get_main()
{
  std::string top_module;
  
  if(cmdline.isset("module"))
    top_module=cmdline.get_value("module");
  else if(cmdline.isset("top"))
    top_module=cmdline.get_value("top");

  try
  {
    main_symbol=&get_module(symbol_table, top_module, get_message_handler());
    trans_expr=to_trans_expr(main_symbol->value);
  }

  catch(int e)
  {
    return true;
  }

  return false;
}

/*******************************************************************\

Function: ebmc_baset::do_bmc

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

int ebmc_baset::do_bmc(prop_convt &solver, bool convert_only)
{
  solver.set_message_handler(get_message_handler());
  
  int result=0;

  try
  {
    if(cmdline.isset("max-bound"))
    {
      if(convert_only)
        throw "please set a specific bound";
        
      const unsigned max_bound=
        unsafe_string2unsigned(cmdline.get_value("max-bound"));
    
      for(bound=1; bound<=max_bound; bound++)
      {
        status() << "Doing BMC with bound " << bound << eom;
        
        #if 0
        const namespacet ns(symbol_table);
        ::unwind(trans_expr, *this, solver, bound+1, ns, true);
        result=finish_bmc(solver);
        #endif
      }

      report_results();
    }
    else
    {
      if(get_bound()) return 1;
    
      if(!convert_only)
        if(properties.empty())
          throw "no properties";

      status() << "Generating Decision Problem" << eom;

      const namespacet ns(symbol_table);
      ::unwind(trans_expr, *this, solver, bound+1, ns, true);

      if(convert_only)
        result=0;
      else
      {
        result=finish_bmc(solver);
        report_results();
      }
    }
  }
    
  catch(const char *e)
  {
    error() << e << eom;
    return 10;
  }
  
  catch(const std::string &e)
  {
    error() << e << eom;
    return 10;
  }
  
  catch(int)
  {
    return 10;
  }  

  return result;
}

/*******************************************************************\

Function: ebmc_baset::do_bmc

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

int ebmc_baset::do_bmc(cnft &solver, bool convert_only)
{
  solver.set_message_handler(get_message_handler());

  if(get_bound()) return 1;

  int result;

  try
  {
    bmc_mapt bmc_map;
  
    if(!convert_only)
      if(properties.empty())
        throw "no properties";
      
    netlistt netlist;
    if(make_netlist(netlist))
      throw 0;

    status() << "Unwinding Netlist" << eom;
    
    bmc_map.map_timeframes(netlist, bound+1, solver);

    ::unwind(netlist, bmc_map, *this, solver);
    
    if(convert_only)
      result=0;
    else
    {
      result=finish_bmc(bmc_map, solver);
      report_results();
    }
  }

  catch(const char *e)
  {
    error() << e << eom;
    return 10;
  }
  
  catch(const std::string &e)
  {
    error() << e << eom;
    return 10;
  }
  
  catch(int)
  {
    return 10;
  }  

  return result;
}

/*******************************************************************\

Function: ebmc_baset::get_model

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

int ebmc_baset::get_model()
{
  // do -I
  if(cmdline.isset('I'))
    config.verilog.include_paths=cmdline.get_values('I');

  //
  // parsing
  //

  if(parse()) return 1;

  if(cmdline.isset("show-parse"))
  {
    language_files.show_parse(std::cout);
    return 0;
  }

  //
  // type checking
  //

  if(typecheck()) 
    return 2;

  if(cmdline.isset("show-modules"))
  {
    show_modules(symbol_table, get_ui());
    return 0;
  }

  if(cmdline.isset("show-symbol-table"))
  {
    std::cout << symbol_table;
    return 0;
  }

  // get module name

  if(get_main()) return 1;

  if(cmdline.isset("show-varmap"))
  {
    netlistt netlist;
    if(make_netlist(netlist)) return 1;
    netlist.var_map.output(std::cout);
    return 0;
  }

  if(cmdline.isset("show-ldg"))
  {
    show_ldg(std::cout);
    return 0;
  }
  
  // --reset given?
  if(cmdline.isset("reset"))
  {
    namespacet ns(symbol_table);
    exprt reset_constraint=to_expr(ns, main_symbol->name, cmdline.get_value("reset"));

    // true in initial state
    trans_expr.init()=and_exprt(trans_expr.init(), reset_constraint);
    
    // and not anymore afterwards
    exprt reset_next_state=reset_constraint;
    make_next_state(reset_next_state);
    
    trans_expr.trans()=and_exprt(trans_expr.trans(), not_exprt(reset_next_state));
  }

  // Property given on command line?
  if(cmdline.isset('p'))
  {
    // NuSMV also uses -p
    if(parse_property(cmdline.get_value('p')))
      return 1;
  }
  else
  {
    // get properties from file
    if(get_model_properties())
      return 1;
  }

  if(cmdline.isset("show-properties"))
  {
    show_properties();
    return 0;
  }

  if(cmdline.isset("show-netlist"))
  {
    netlistt netlist;
    if(make_netlist(netlist)) return 1;
    netlist.print(std::cout);
    return 0;
  }
  
  if(cmdline.isset("smv-netlist"))
  {
    netlistt netlist;
    if(make_netlist(netlist)) return 1;
    std::cout << "-- Generated by EBMC " << EBMC_VERSION << '\n';
    std::cout << "-- Generated from " << main_symbol->name << '\n';
    std::cout << '\n';
    netlist.output_smv(std::cout);
    return 0;
  }
  
  if(cmdline.isset("dot-netlist"))
  {
    netlistt netlist;
    if(make_netlist(netlist)) return 1;
    std::cout << "digraph netlist {\n";
    netlist.output_dot(std::cout);
    std::cout << "}\n";
    return 0;
  }
  
  return -1; // done with the model
}

/*******************************************************************\

Function: ebmc_baset::show_ldg

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void ebmc_baset::show_ldg(std::ostream &out)
{
  netlistt netlist;

  if(make_netlist(netlist))
    return;
  
  if(!netlist.transition.empty())
    out << "WARNING: transition constraint found!" << '\n'
        << '\n';
  
  ldgt ldg;
 
  ldg.compute(netlist);
    
  out << "Latch dependencies:" << '\n';

  for(var_mapt::mapt::const_iterator
      it=netlist.var_map.map.begin();
      it!=netlist.var_map.map.end();
      it++)
  {
    const var_mapt::vart &var=it->second;

    for(std::size_t i=0; i<var.bits.size(); i++)
    {
      if(var.is_latch())
      {
        literalt::var_not v=var.bits[i].current.var_no();

        out << "  " << it->first
            << "[" << i << "] = " << v << ":";

        const ldg_nodet &node=ldg[v];

        for(ldg_nodet::edgest::const_iterator
            i_it=node.in.begin();
            i_it!=node.in.end();
            i_it++)
          out << " " << i_it->first;

        out << '\n';
      }
    }
  }
}

/*******************************************************************\

Function: ebmc_baset::make_netlist

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool ebmc_baset::make_netlist(netlistt &netlist)
{
  // make net-list
  status() << "Generating Netlist" << eom;

  try
  {
    convert_trans_to_netlist(
      symbol_table, main_symbol->name,
      netlist, get_message_handler());
  }
  
  catch(const std::string &error_str)
  {
    error() << error_str << eom;
    return true;
  }

  statistics() << "Latches: " << netlist.var_map.latches.size()
               << ", nodes: " << netlist.number_of_nodes() << eom;
           
  return false;
}

/*******************************************************************\

Function: ebmc_baset::do_compute_ct

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

int ebmc_baset::do_compute_ct()
{
  // make net-list
  status() << "Making Netlist" << eom;

  netlistt netlist;
  if(make_netlist(netlist)) return 1;

  status() << "Latches: " << netlist.var_map.latches.size()
           << ", nodes: " << netlist.number_of_nodes() << eom;

  status() << "Making LDG" << eom;
  
  ldgt ldg;
  ldg.compute(netlist);

  std::cout << "CT = " << compute_ct(ldg) << '\n';
  
  return 0;
}

/*******************************************************************\

Function: ebmc_baset::report_results

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void ebmc_baset::report_results()
{
  const namespacet ns(symbol_table);

  if(get_ui()==ui_message_handlert::uit::XML_UI)
  {
    for(const propertyt &property : properties)
    {
      if(property.status==propertyt::statust::DISABLED)
        continue;
        
      xmlt xml_result("result");
      xml_result.set_attribute("property", id2string(property.name));
      
      switch(property.status)
      {
      case propertyt::statust::SUCCESS: xml_result.set_attribute("status", "SUCCESS"); break;
      case propertyt::statust::FAILURE: xml_result.set_attribute("status", "FAILURE"); break;
      case propertyt::statust::UNKNOWN: xml_result.set_attribute("status", "UNKNOWN"); break;
      case propertyt::statust::DISABLED:;
      }
      
      if(property.is_failure())
        convert(ns, property.counterexample, xml_result.new_element());

      std::cout << xml_result << '\n' << std::flush;
    }
  }
  else
  {
    status() << eom;
    status() << "** Results:" << eom;

    for(const propertyt &property : properties)
    {
      if(property.status==propertyt::statust::DISABLED)
        continue;
      
      status() << "[" << property.name << "] "
               << property.expr_string << ": ";

      switch(property.status)
      {
      case propertyt::statust::SUCCESS: status() << "SUCCESS"; break;
      case propertyt::statust::FAILURE: status() << "FAILURE"; break;
      case propertyt::statust::UNKNOWN: status() << "UNKNOWN"; break;
      case propertyt::statust::DISABLED:;
      }
               
      status() << eom;
      
      if(property.is_failure() &&
         cmdline.isset("trace"))
      {
        status() << "Counterexample:\n" << eom;
        show_trans_trace(
          property.counterexample, *this, ns, get_ui());
      }
    }
  }

  if(cmdline.isset("vcd"))
  {
    for(const propertyt &property : properties)
    {
      if(property.is_failure())
      {
        std::string vcdfile=cmdline.get_value("vcd");
        #ifdef _MSC_VER
        std::ofstream vcd(widen(vcdfile));
        #else    
        std::ofstream vcd(vcdfile);
        #endif

        show_trans_trace_vcd(
          property.counterexample,
          *this, // message
          ns,
          vcd);

        break;
      }
    }
  }

}

