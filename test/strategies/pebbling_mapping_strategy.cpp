#include <catch.hpp>

#include <cstdint>

#include <caterpillar/structures/stg_gate.hpp>
#include <caterpillar/structures/pebbling_view.hpp>
#include <caterpillar/synthesis/lhrs.hpp>
#include <caterpillar/synthesis/strategies/pebbling_mapping_strategy.hpp>
#include <caterpillar/solvers/bsat_solver.hpp>
#include <caterpillar/solvers/z3_solver.hpp>
#include <caterpillar/verification/circuit_to_logic_network.hpp>
#include <kitty/static_truth_table.hpp>
#include <mockturtle/algorithms/simulation.hpp>
#include <mockturtle/networks/aig.hpp>
#include <mockturtle/networks/xag.hpp>
#include <tweedledum/io/write_unicode.hpp>
#include <tweedledum/networks/netlist.hpp>

TEST_CASE( "Pebble mapping strategy for 3-bit sorting network bsat", "[pebbling_mapping_strategy1]" )
{
  using namespace caterpillar;
  using namespace caterpillar::detail;
  using namespace mockturtle;
  using namespace tweedledum;

  aig_network sorter;
  const auto a = sorter.create_pi();
  const auto b = sorter.create_pi();
  const auto c = sorter.create_pi();

  const auto w1 = sorter.create_and( a, b );
  const auto w2 = sorter.create_and( c, w1 );
  const auto w3 = sorter.create_and( !a, !b );
  const auto w4 = sorter.create_and( !c, !w1 );
  const auto w5 = sorter.create_and( !w3, !w4 );
  const auto w6 = sorter.create_or( c, !w3 );

  sorter.create_po( w2 );
  sorter.create_po( w5 );
  sorter.create_po( w6 );

  netlist<stg_gate> circ;
  pebbling_mapping_strategy<aig_network, bsat_pebble_solver<aig_network>> strategy;
  logic_network_synthesis_stats st;
  logic_network_synthesis(circ, sorter, strategy, {}, {}, &st);

  CHECK( circ.num_gates() != 0 );

  const auto sorter2 = circuit_to_logic_network<aig_network>(circ, st.i_indexes, st.o_indexes);
  CHECK( sorter2 );
  CHECK( simulate<kitty::static_truth_table<3>>( sorter ) == simulate<kitty::static_truth_table<3>>( *sorter2 ) );
}

//#ifdef USE_Z3
TEST_CASE( "Pebble mapping strategy for 3-bit sorting network z3", "[pebbling_mapping_strategy2]" )
{
  using namespace caterpillar;
  using namespace caterpillar::detail;
  using namespace mockturtle;
  using namespace tweedledum;

  aig_network sorter;
  const auto a = sorter.create_pi();
  const auto b = sorter.create_pi();
  const auto c = sorter.create_pi();

  const auto w1 = sorter.create_and( a, b );
  const auto w2 = sorter.create_and( c, w1 );
  const auto w3 = sorter.create_and( !a, !b );
  const auto w4 = sorter.create_and( !c, !w1 );
  const auto w5 = sorter.create_and( !w3, !w4 );
  const auto w6 = sorter.create_or( c, !w3 );

  sorter.create_po( w2 );
  sorter.create_po( w5 );
  sorter.create_po( w6 );

  netlist<stg_gate> circ;

  pebbling_mapping_strategy_params psp;
  psp.pebble_limit = 4;
  pebbling_mapping_strategy<aig_network, z3_pebble_solver<aig_network>> strategy (psp);

  logic_network_synthesis_stats st;
  logic_network_synthesis( circ, sorter, strategy, {}, {}, &st );

  CHECK( circ.num_gates() != 0 );

  const auto sorter2 = circuit_to_logic_network<aig_network>(circ, st.i_indexes, st.o_indexes);
  CHECK( sorter2 );
  CHECK( simulate<kitty::static_truth_table<3>>( sorter ) == simulate<kitty::static_truth_table<3>>( *sorter2 ) );
}
//#endif

TEST_CASE("pebble xag using weighted nodes", "[peb. xag with weights]")
{
  using namespace mockturtle;
  using namespace caterpillar;
  using namespace tweedledum;
  using peb_xag_t = pebbling_view<xag_network>;

  xag_network xag;

  auto p1 = xag.create_pi();
  auto p2 = xag.create_pi();
  auto p3 = xag.create_pi();
  auto p4 = xag.create_pi();

  auto n1 = xag.create_and(p1, p2);
  auto n2 = xag.create_xor(p2, p3);
  auto n3 = xag.create_xor(p3, p4);
  auto n4 = xag.create_and(n1, n2);
  auto n5 = xag.create_xor(n3, n4);

  xag.create_po(n5);

  peb_xag_t peb_xag = pebbling_view{xag};
  peb_xag.set_weight(xag.get_node(n1), 4);

  pebbling_mapping_strategy_params ps;
  ps.pebble_limit = 4;
  ps.max_weight = 17;
  weighted_pebbling_mapping_strategy<peb_xag_t> strategy (ps);

  netlist<stg_gate> rnet;

  logic_network_synthesis_stats st;
  logic_network_synthesis_params param;
  param.verbose = false;


  logic_network_synthesis(rnet, peb_xag, strategy, {}, param, &st);

  const auto circ = circuit_to_logic_network<xag_network>(rnet, st.i_indexes, st.o_indexes);
  CHECK( circ );
  CHECK( simulate<kitty::static_truth_table<4>>( xag ) == simulate<kitty::static_truth_table<4>>( *circ ) );

}