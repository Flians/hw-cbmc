/******************************************************

Module: Lifting states (i.e. turning states into 
        cubes of states)

Author: Eugene Goldberg, eu.goldberg@gmail.com

******************************************************/
#include <iostream>
#include <queue>
#include <set>
#include <map>
#include <algorithm>
#include "Solver.h"
#include "SimpSolver.h"
#include "dnf_io.hh"
#include "ccircuit.hh"
#include "m0ic3.hh"


/*=======================================

  L I F T _ G O O D_ S T A T E

  ======================================*/
void CompInfo::lift_good_state(CUBE &Gst_cube,CUBE &Prs_st,CUBE &Inps,
			       CUBE &Nst_cube)
{

  // add unit clauses specifying inputs
  MvecLits Assmps;
  add_assumps1(Assmps,Inps);
  

  // add clause excluding next state cube
  CUBE Mapped_cube;

 
  conv_to_next_state(Mapped_cube,Nst_cube);
  
  Mlit act_lit;  
  add_cls_excl_st_cube(act_lit,Lgs_sat,Mapped_cube);
 
  Assmps.push(act_lit);
  add_assumps2(Assmps,Prs_st);
  
  bool sat_form = check_sat2(Lgs_sat,Assmps);
  if (sat_form) {
    p();
    std::cout << "Inps-> " << Inps << std::endl;
    std::cout << "Prs_st-> " << Prs_st << std::endl;
    std::cout << "Mapped_cube-> " << Mapped_cube << std::endl;
    fprint_srt_dnf(Simp_PrTr,(char *) "simp.cnf");    
    exit(100);
  }
  
  gen_state_cube(Gst_cube,Prs_st,Lgs_sat);

  release_lit(Lgs_sat,~act_lit);

  num_gstate_cubes++;
  length_gstate_cubes += Gst_cube.size();

} /* end of function lift_good_state */

/*========================================

  L I F T _ B A D _ S T A T E 

  =========================================*/
void CompInfo::lift_bad_state(CUBE &Bst_cube,CUBE &St,CUBE &Inps)
{

  TrivMclause Assmps;
  add_assumps1(Assmps,Inps);

  add_assumps2(Assmps,St);

 
  bool sat_form = check_sat2(Lbs_sat,Assmps);
 
  assert(sat_form == false);

  gen_state_cube(Bst_cube,St,Lbs_sat);
 
  num_bstate_cubes++;
  length_bstate_cubes += Bst_cube.size();

} /* end of function lift_bad_state */


/*==========================================

  G E N _ S T A T E _ C U B E 
 
  ASSUMPTIONS:
  1) Formula S.D is unsatisfiable
  2) S.Proof is a proof of that

  ========================================*/
void CompInfo::gen_state_cube(CUBE &St_cube,CUBE &St,SatSolver &Slvr)
{
 
  Minisat::Solver *Mst = Slvr.Mst;
  for (int i=0; i < St.size(); i++) {
    Mlit L = conv_to_mlit(St[i]);
    if (Mst->conflict.has(~L)) {
      St_cube.push_back(St[i]); 
    } 	
  }	

  
} /* end of function gen_state_cube */


/*==========================================

  A D D _ C L S _ E X C L _ S T _ C U B E

  ===========================================*/
void CompInfo::add_cls_excl_st_cube(Mlit &act_lit,SatSolver &Slvr,CUBE &St)
{
  CLAUSE C;
  act_lit = Minisat::mkLit(Slvr.Mst->newVar(),false);
  int lit = Minisat::var(act_lit)+1;
  C.push_back(-lit);

  for (int i=0; i < St.size(); i++) {
    int var_ind = abs(St[i])-1;
    C.push_back(-St[i]);
  }

  accept_new_clause(Slvr,C);
  
} /* end of function add_cls_excl_st_cube */

/*=============================================

  E X T R _ P R E S  _ I N P S

  This function returns the set of assignments
  to pres state time frame inputs

  ASSUMPTIONS:
  1) Sat-solver 'S' just proved formula satisfiable
  2) Assignment returned by 'S' is actually the
  negation of a satisfying assignment
    
  =============================================*/
void CompInfo::extr_pres_inps(CUBE &Inps,SatSolver &Slvr)
{


  MboolVec &S = Slvr.Mst->model;

  for (int i=0; i < Inp_vars.size(); i++) {
    int var_ind = Inp_vars[i]-1;
    if (S[var_ind] == Mtrue) Inps.push_back(var_ind+1);
    else Inps.push_back(-(var_ind+1));
  }

} /* end of function extr_pres_inps */

/*=============================================

  E X T R _ N E X T _ I N P S

  This function returns the set of assignments
  to next state time frame inputs mapped to present
  state time frame.

  ASSUMPTIONS:
  1) Sat-solver 'S' just proved formula satisfiable
  2) Input variables of the next state time frame
  are those of the present time frame shifted
  by 'max_num_vars0'
  3) Assignment returned by 'S' is actually the
  negation of a satisfying assignment
    
  =============================================*/
void CompInfo::extr_next_inps(CUBE &Inps,SatSolver &Slvr)
{

  MboolVec &S = Slvr.Mst->model;

  for (int i=0; i < Inp_vars.size(); i++) {
    int orig_var_ind = Inp_vars[i]-1;
    int var_ind = orig_var_ind + max_num_vars0;
    assert(var_ind < max_num_vars);
    if (S[var_ind] == Mtrue) Inps.push_back(orig_var_ind+1);
    else Inps.push_back(-(orig_var_ind+1));
  }

 
} /* end of function extr_next_inps */