/*------------------------------------------------------------------------------
| This file is distributed under the MIT License.
| See accompanying file /LICENSE for details.
| Author(s): Mathias Soeken and Giulia Meuli
*-----------------------------------------------------------------------------*/

#pragma once

#include <cstdint>
#include <iostream>
#include <unordered_set>

#include "action.hpp"
#include "mapping_strategy.hpp"
#include "../sat.hpp"

#include <mockturtle/utils/progress_bar.hpp>
#include <mockturtle/views/topo_view.hpp>

#include <fmt/format.h>

namespace caterpillar
{

namespace detail
{
template<class... Ts>
struct overloaded : Ts...
{
  using Ts::operator()...;
};
template<class... Ts>
overloaded( Ts... )->overloaded<Ts...>;
} // namespace detail

namespace mt = mockturtle;

template<class LogicNetwork>
class pebbling_mapping_strategy : public mapping_strategy<LogicNetwork>
{
public:
  using base_t = mapping_strategy<LogicNetwork>;

  pebbling_mapping_strategy( LogicNetwork const& ntk, mapping_strategy_params const& ps = {} )
      : mapping_strategy<LogicNetwork>( ntk, ps )
  {
    static_assert( mt::is_network_type_v<LogicNetwork>, "LogicNetwork is not a network type" );
    static_assert( mt::has_is_pi_v<LogicNetwork>, "LogicNetwork does not implement the is_pi method" );
    static_assert( mt::has_foreach_fanin_v<LogicNetwork>, "LogicNetwork does not implement the foreach_fanin method" );
    static_assert( mt::has_foreach_gate_v<LogicNetwork>, "LogicNetwork does not implement the foreach_gate method" );
    static_assert( mt::has_num_gates_v<LogicNetwork>, "LogicNetwork does not implement the num_gates method" );
    static_assert( mt::has_foreach_po_v<LogicNetwork>, "LogicNetwork does not implement the foreach_po method" );
    static_assert( mt::has_index_to_node_v<LogicNetwork>, "LogicNetwork does not implement the index_to_node method" );
  }

  virtual bool foreach_step( typename base_t::step_function_t const& fn ) const override
  {
    assert( !this->_ps.decrement_on_success || !this->_ps.increment_on_timeout );
    std::vector<std::pair<mockturtle::node<LogicNetwork>, mapping_strategy_action>> store_steps;
    auto limit = this->_ps.pebble_limit;
    int max_steps = 100;
    while ( true )
    {
      pebble_solver<LogicNetwork> solver( this->_ntk, limit );
      solver.initialize();

      mockturtle::progress_bar bar( 100, "|{0}| current step = {1}", this->_ps.progress );
      percy::synth_result result;

      do
      {
        if ( solver.current_step() >= max_steps )
        {
          result = percy::timeout;
          break;
        }

        bar( std::min<uint32_t>( solver.current_step(), 100 ), solver.current_step() );
        solver.add_step();
        result = solver.solve( this->_ps.conflict_limit );
      } while ( result == percy::failure );

      if ( result == percy::timeout )
      {
        if ( this->_ps.increment_on_timeout )
        {
          limit++;
          continue;
        }
        else if ( !this->_ps.decrement_on_success )
          return false;
      }
      else if ( result == percy::success )
      {
        store_steps = solver.extract_result();
        if ( this->_ps.decrement_on_success )
        {
          limit--;
          continue;
        }
      }

      if ( store_steps.empty() )
        return false;

      for ( auto const& [n, a] : store_steps )
      {
        fn( n, a );
      }

      return true;
    }
  }
};

template<class LogicNetwork>
class bennett_mapping_strategy : public mapping_strategy<LogicNetwork>
{
public:
  bennett_mapping_strategy()
  {
    static_assert( mt::is_network_type_v<LogicNetwork>, "LogicNetwork is not a network type" );
    static_assert( mt::has_foreach_po_v<LogicNetwork>, "LogicNetwork does not implement the foreach_po method" );
    static_assert( mt::has_is_constant_v<LogicNetwork>, "LogicNetwork does not implement the is_constant method" );
    static_assert( mt::has_is_pi_v<LogicNetwork>, "LogicNetwork does not implement the is_pi method" );
    static_assert( mt::has_get_node_v<LogicNetwork>, "LogicNetwork does not implement the get_node method" );
  }

  bool compute_steps( LogicNetwork const& ntk ) override
  {
    std::unordered_set<mt::node<LogicNetwork>> drivers;
    ntk.foreach_po( [&]( auto const& f ) { drivers.insert( ntk.get_node( f ) ); } );

    auto it = this->steps().begin();
    mt::topo_view view{ntk};
    view.foreach_node( [&]( auto n ) {
      if ( ntk.is_constant( n ) || ntk.is_pi( n ) )
        return true;

      /* compute step */
      it = this->steps().insert( it, {n, compute_action{}} );
      ++it;

      if ( !drivers.count( n ) )
        it = this->steps().insert( it, {n, uncompute_action{}} );

      return true;
    } );

    return true;
  }
};

template<class LogicNetwork>
class bennett_inplace_mapping_strategy : public mapping_strategy<LogicNetwork>
{
public:
  bennett_inplace_mapping_strategy()
  {
    static_assert( mt::is_network_type_v<LogicNetwork>, "LogicNetwork is not a network type" );
    static_assert( mt::has_foreach_po_v<LogicNetwork>, "LogicNetwork does not implement the foreach_po method" );
    static_assert( mt::has_is_constant_v<LogicNetwork>, "LogicNetwork does not implement the is_constant method" );
    static_assert( mt::has_is_pi_v<LogicNetwork>, "LogicNetwork does not implement the is_pi method" );
    static_assert( mt::has_get_node_v<LogicNetwork>, "LogicNetwork does not implement the get_node method" );
    static_assert( mt::has_node_to_index_v<LogicNetwork>, "LogicNetwork does not implement the node_to_index method" );
    static_assert( mt::has_clear_values_v<LogicNetwork>, "LogicNetwork does not implement the clear_values method" );
    static_assert( mt::has_set_value_v<LogicNetwork>, "LogicNetwork does not implement the set_value method" );
    static_assert( mt::has_decr_value_v<LogicNetwork>, "LogicNetwork does not implement the decr_value method" );
    static_assert( mt::has_fanout_size_v<LogicNetwork>, "LogicNetwork does not implement the fanout_size method" );
    static_assert( mt::has_foreach_fanin_v<LogicNetwork>, "LogicNetwork does not implement the foreach_fanin method" );
  }

  bool compute_steps( LogicNetwork const& ntk ) override
  {
    std::unordered_set<mt::node<LogicNetwork>> drivers;
    ntk.foreach_po( [&]( auto const& f ) { drivers.insert( ntk.get_node( f ) ); } );

    ntk.clear_values();
    ntk.foreach_node( [&]( const auto& n ) { ntk.set_value( n, ntk.fanout_size( n ) ); } );

    auto it = this->steps().begin();
    //mt::topo_view view{ntk};
    ntk.foreach_node( [&]( auto n ) {
      if ( ntk.is_constant( n ) || ntk.is_pi( n ) )
        return true;

      /* decrease reference counts and mark potential target for inplace */
      int target{-1};
      ntk.foreach_fanin( n, [&]( auto f ) {
        if ( ntk.decr_value( ntk.get_node( f ) ) == 0 )
        {
          if ( target == -1 )
          {
            target = ntk.node_to_index( ntk.get_node( f ) );
          }
        }
      } );

      /* check for inplace (only if there is possible target and node is not an output driver) */
      if ( target != -1 && !drivers.count( n ) )
      {
        if constexpr ( mt::has_is_xor_v<LogicNetwork> )
        {
          if ( ntk.is_xor( n ) )
          {
            it = this->steps().insert( it, {n, compute_inplace_action{
                                           static_cast<uint32_t>(
                                               target )}} );
            ++it;
            it = this->steps().insert( it, {n, uncompute_inplace_action{
                                           static_cast<uint32_t>(
                                               target )}} );
            return true;
          }
        }
        if constexpr ( mt::has_is_xor3_v<LogicNetwork> )
        {
          if ( ntk.is_xor3( n ) )
          {
            it = this->steps().insert( it, {n, compute_inplace_action{
                                           static_cast<uint32_t>(
                                               target )}} );
            ++it;
            it = this->steps().insert( it, {n, uncompute_inplace_action{
                                           static_cast<uint32_t>(
                                               target )}} );
            return true;
          }
        }
      }

      /* compute step */
      it = this->steps().insert( it, {n, compute_action{}} );
      ++it;

      if ( !drivers.count( n ) )
        it = this->steps().insert( it, {n, uncompute_action{}} );

      return true;
    } );

    return true;
  }
};

template<class MappingStrategy>
void print_mapping_strategy( MappingStrategy const& strategy, std::ostream& os = std::cout )
{
  strategy.foreach_step( [&]( auto node, auto action ) {
    std::visit(
        detail::overloaded{
            []( auto ) {},
            [&]( compute_action const& ) {
              os << fmt::format( "compute({})\n", node );
            },
            [&]( uncompute_action const& ) {
              os << fmt::format( "uncompute({})\n", node );
            },
            [&]( compute_inplace_action const& action ) {
              os << fmt::format( "compute_inplace({} -> {})\n", node, action.target_index );
            },
            [&]( uncompute_inplace_action const& action ) {
              os << fmt::format( "uncompute_inplace({} -> {})\n", node, action.target_index );
            }},
        action );
  } );
}

} // namespace caterpillar