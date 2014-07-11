#include <bts/blockchain/chain_interface.hpp>
#include <bts/blockchain/exceptions.hpp>
#include <bts/blockchain/market_operations.hpp>

namespace bts { namespace blockchain {

   /**
    *  If the amount is negative then it will withdraw/cancel the bid assuming
    *  it is signed by the owner and there is sufficient funds.
    *
    *  If the amount is positive then it will add funds to the bid.
    */
   void bid_operation::evaluate( transaction_evaluation_state& eval_state )
   { try {
      if( this->bid_index.order_price == price() )
         FC_CAPTURE_AND_THROW( zero_price, (bid_index.order_price) );

      auto owner = this->bid_index.owner;
      if( !eval_state.check_signature( owner ) )
         FC_CAPTURE_AND_THROW( missing_signature, (bid_index.owner) );

      asset delta_amount  = this->get_amount();

      eval_state.validate_asset( delta_amount );

      auto current_bid   = eval_state._current_state->get_bid_record( this->bid_index );

      if( this->amount == 0 ) FC_CAPTURE_AND_THROW( zero_amount );
      if( this->amount <  0 ) // withdraw
      {
          if( NOT current_bid ) 
             FC_CAPTURE_AND_THROW( unknown_market_order, (bid_index) );

          if( abs(this->amount) > current_bid->balance )
             FC_CAPTURE_AND_THROW( insufficient_funds, (amount)(current_bid->balance) );

          // add the delta amount to the eval state that we withdrew from the bid
          eval_state.add_balance( -delta_amount );
      }
      else // this->amount > 0 - deposit
      {
          if( NOT current_bid )  // then initialize to 0
            current_bid = order_record();
          // sub the delta amount from the eval state that we deposited to the bid
          eval_state.sub_balance( balance_id_type(), delta_amount );
      }

      current_bid->balance     += this->amount;

      eval_state._current_state->store_bid_record( this->bid_index, *current_bid );

      //auto check   = eval_state._current_state->get_bid_record( this->bid_index );
   } FC_CAPTURE_AND_RETHROW( (*this) ) }

   /**
    *  If the amount is negative then it will withdraw/cancel the bid assuming
    *  it is signed by the owner and there is sufficient funds.
    *
    *  If the amount is positive then it will add funds to the bid.
    */
   void ask_operation::evaluate( transaction_evaluation_state& eval_state )
   { try {
      if( this->ask_index.order_price == price() )
         FC_CAPTURE_AND_THROW( zero_price, (ask_index.order_price) );

      auto owner = this->ask_index.owner;
      if( !eval_state.check_signature( owner ) )
         FC_CAPTURE_AND_THROW( missing_signature, (ask_index.owner) );

      asset delta_amount  = this->get_amount();

      eval_state.validate_asset( delta_amount );

      auto current_ask   = eval_state._current_state->get_ask_record( this->ask_index );


      if( this->amount == 0 ) FC_CAPTURE_AND_THROW( zero_amount );
      if( this->amount <  0 ) // withdraw
      {
          if( NOT current_ask ) 
             FC_CAPTURE_AND_THROW( unknown_market_order, (ask_index) );

          if( abs(this->amount) > current_ask->balance )
             FC_CAPTURE_AND_THROW( insufficient_funds, (amount)(current_ask->balance) );

          // add the delta amount to the eval state that we withdrew from the ask
          eval_state.add_balance( -delta_amount );
      }
      else // this->amount > 0 - deposit
      {
          if( NOT current_ask )  // then initialize to 0
            current_ask = order_record();
          // sub the delta amount from the eval state that we deposited to the ask
          eval_state.sub_balance( balance_id_type(), delta_amount );
      }
      
      current_ask->balance     += this->amount;

      eval_state._current_state->store_ask_record( this->ask_index, *current_ask );

      //auto check   = eval_state._current_state->get_ask_record( this->ask_index );
   } FC_CAPTURE_AND_RETHROW( (*this) ) }

   void short_operation::evaluate( transaction_evaluation_state& eval_state )
   {
      if( this->short_index.order_price == price() )
         FC_CAPTURE_AND_THROW( zero_price, (short_index.order_price) );

      auto owner = this->short_index.owner;
      if( !eval_state.check_signature( owner ) )
         FC_CAPTURE_AND_THROW( missing_signature, (short_index.owner) );

      asset delta_amount  = this->get_amount();
      wdump( (delta_amount) );

      eval_state.validate_asset( delta_amount );

      auto current_short   = eval_state._current_state->get_short_record( this->short_index );
      if( current_short ) wdump( (current_short) );

      if( this->amount == 0 ) FC_CAPTURE_AND_THROW( zero_amount );
      if( this->amount <  0 ) // withdraw
      {
          if( NOT current_short ) 
             FC_CAPTURE_AND_THROW( unknown_market_order, (short_index) );

          if( abs(this->amount) > current_short->balance )
             FC_CAPTURE_AND_THROW( insufficient_funds, (amount)(current_short->balance) );

          // add the delta amount to the eval state that we withdrew from the short
          eval_state.add_balance( -delta_amount );
      }
      else // this->amount > 0 - deposit
      {
          if( NOT current_short )  // then initialize to 0
            current_short = order_record();
          // sub the delta amount from the eval state that we deposited to the short
          eval_state.sub_balance( balance_id_type(), delta_amount );
      }
      
      wdump( (amount) );
      current_short->balance     += this->amount;

      eval_state._current_state->store_short_record( this->short_index, *current_short );

      //auto check   = eval_state._current_state->get_ask_record( this->ask_index );
   }

} } // bts::blockchain
