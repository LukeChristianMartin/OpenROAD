source "helpers.tcl"
set LIB_DIR "./Nangate45"
#
set tech_lef "$LIB_DIR/Nangate45_tech.lef"
set std_cell_lef "$LIB_DIR/Nangate45.lef"
set fake_macro_lef "$LIB_DIR/fake_macros.lef"
set liberty_file "$LIB_DIR/Nangate45_fast.lib"
set fake_macro_lib "$LIB_DIR/fake_macros.lib"
 
set synth_verilog "./testcases/mp_test1.v"
set floorplan_def "./testcases/mp_test1_fp.def"
set top_module "mp_test1"
 
read_lef $tech_lef
read_lef $std_cell_lef
read_lef $fake_macro_lef
read_liberty $liberty_file
read_liberty $fake_macro_lib
 
read_verilog $synth_verilog
link_design $top_module
#
read_def $floorplan_def -floorplan_initialize

set_thread_count 0
rtl_macro_placer -report_directory results/mp_test1 -halo_width 5.0

set def_file [make_result_file mp_test1.def]
write_def $def_file

diff_files mp_test1.defok $def_file
