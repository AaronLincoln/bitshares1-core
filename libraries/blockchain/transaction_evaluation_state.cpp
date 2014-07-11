#include <bts/blockchain/chain_interface.hpp>
#include <bts/blockchain/operation_factory.hpp>
#include <bts/blockchain/transaction_evaluation_state.hpp>

namespace bts { namespace blockchain { 

   transaction_evaluation_state::transaction_evaluation_state( const chain_interface_ptr& current_state, digest_type chain_id )
   :_current_state( current_state ),_chain_id(chain_id),_skip_signature_check(false)
   {
   }

   transaction_evaluation_state::~transaction_evaluation_state()
   {
   }

   void transaction_evaluation_state::reset()
   {
      signed_keys.clear();
      balance.clear();
      deposits.clear();
      withdraws.clear();
      net_delegate_votes.clear();
      required_deposits.clear();
      validation_error.reset();
   }

   bool transaction_evaluation_state::check_signature( const address& a )const
   {
      return  _skip_signature_check || signed_keys.find( a ) != signed_keys.end();
   }

   void transaction_evaluation_state::verify_delegate_id( account_id_type id )const
   {
      auto current_account = _current_state->get_account_record( id );
      if( !current_account ) FC_CAPTURE_AND_THROW( unknown_account_id, (id) );
      if( !current_account->is_delegate() ) FC_CAPTURE_AND_THROW( not_a_delegate, (id) );
   }

   void transaction_evaluation_state::add_required_deposit( const address& owner_key, const asset& amount )
   {
      FC_ASSERT( trx.delegate_slate_id );
      balance_id_type balance_id = withdraw_condition( 
                                       withdraw_with_signature( owner_key ), 
                                       amount.asset_id, *trx.delegate_slate_id ).get_address();

      auto itr = required_deposits.find( balance_id );
      if( itr == required_deposits.end() )
      {
         required_deposits[balance_id] = amount;
      }
      else
      {
         required_deposits[balance_id] += amount;
      }
   }

   void transaction_evaluation_state::update_delegate_votes()
   {
      auto asset_rec = _current_state->get_asset_record( asset_id_type() );

      for( auto del_vote : net_delegate_votes )
      {
         auto del_rec = _current_state->get_account_record( del_vote.first );
         FC_ASSERT( !!del_rec );
         del_rec->adjust_votes_for( del_vote.second.votes_for );

         _current_state->store_account_record( *del_rec );
      }
   }

   void transaction_evaluation_state::validate_required_fee()
   { try {
      share_type required_fee = _current_state->calculate_data_fee( trx.data_size() );
      auto fee_itr = balance.find( 0 );
      if( fee_itr == balance.end() ||
          fee_itr->second < required_fee ) 
      {
         FC_CAPTURE_AND_THROW( insufficient_fee, (required_fee) );
      }
   } FC_RETHROW_EXCEPTIONS( warn, "" ) }

   /**
    *  Process all fees and update the asset records.
    */
   void transaction_evaluation_state::post_evaluate()
   { try {
      required_fees += asset(_current_state->calculate_data_fee(fc::raw::pack_size(trx)),0);

      balance[0]; // make sure we have something for this.
      for( auto fee : balance )
      {
         if( fee.second < 0 ) FC_CAPTURE_AND_THROW( negative_fee, (fee) );
         if( fee.second > 0 )
         {
            if( fee.first == 0  )
               continue;
            // check to see if there are any open bids to buy the asset
         }

         // lowest ask is someone with XTS offered at a price of USD / XTS, fee.first
         // is an amount of USD which can be converted to price*USD XTS provided we
         // send lowest_ask.index.owner the USD
         omarket_order lowest_ask = _current_state->get_lowest_ask_record( fee.first, 0 );
         if( lowest_ask )
         {
            // do we have enough funds in the ask to cover the fees?
            asset required_order_balance = asset( fee.second, fee.first ) * 
                                            lowest_ask->market_index.order_price;  
            
            if( required_order_balance.amount <= lowest_ask->state.balance )
            {
               balance[0] += required_order_balance.amount;

               auto balance_id = withdraw_condition( 
                                   withdraw_with_signature( lowest_ask->market_index.owner ), 
                                   fee.first       ).get_address(); 


               auto balance_rec = _current_state->get_balance_record( balance_id );
               if( balance_rec )
               {
                  balance_rec->balance += fee.second; 
               }
               else
               {
                  balance_rec = balance_record( lowest_ask->market_index.owner, 
                                              asset( fee.second, fee.first ), 0 );

               }
               _current_state->store_balance_record( *balance_rec );

               lowest_ask->state.balance -= required_order_balance.amount;
               _current_state->store_ask_record( lowest_ask->market_index, lowest_ask->state );
            }
            // trade fee.second 
         }
      }


      for( auto fee : balance )
      {
         if( fee.second < 0 ) FC_CAPTURE_AND_THROW( negative_fee, (fee) );
         if( fee.second > 0 )
         {
            if( fee.first == 0 && fee.second < required_fees.amount )
               FC_CAPTURE_AND_THROW( insufficient_fee, (fee)(required_fees.amount) );

            auto asset_record = _current_state->get_asset_record( fee.first );
            if( !asset_record )
              FC_CAPTURE_AND_THROW( unknown_asset_id, (fee.first) );

            asset_record->collected_fees += fee.second;
            asset_record->current_share_supply -= fee.second;
            _current_state->store_asset_record( *asset_record );
         }
      }


      for( auto required_deposit : required_deposits )
      {
         auto provided_itr = provided_deposits.find( required_deposit.first );
         
         if( provided_itr->second < required_deposit.second )
            FC_CAPTURE_AND_THROW( missing_deposit, (required_deposit) );
      }

   } FC_RETHROW_EXCEPTIONS( warn, "" ) }

   void transaction_evaluation_state::evaluate( const signed_transaction& trx_arg, bool skip_signature_check )
   { try {
      reset();
      _skip_signature_check = skip_signature_check;
      try {
        if( trx_arg.expiration < _current_state->now() )
        {
           auto expired_by_sec = (trx_arg.expiration - _current_state->now()).to_seconds();
           FC_CAPTURE_AND_THROW( expired_transaction, (trx_arg)(_current_state->now())(expired_by_sec) );
        }
        if( trx_arg.expiration > (_current_state->now() + BTS_BLOCKCHAIN_MAX_TRANSACTION_EXPIRATION_SEC) )
           FC_CAPTURE_AND_THROW( invalid_transaction_expiration, (trx_arg)(_current_state->now()) );

        auto trx_size = fc::raw::pack_size(trx_arg);
        if(  trx_size > BTS_BLOCKCHAIN_MAX_TRANSACTION_SIZE )
           FC_CAPTURE_AND_THROW( oversized_transaction, (trx_size ) );
       
        auto trx_id = trx_arg.id();

        if( _current_state->is_known_transaction( trx_id ) )
           FC_CAPTURE_AND_THROW( duplicate_transaction, (trx_id) );
       
        trx = trx_arg;
        if( !_skip_signature_check )
        {
           auto digest = trx_arg.digest( _chain_id );
           for( auto sig : trx.signatures )
           {
              auto key = fc::ecc::public_key( sig, digest ).serialize();
              signed_keys.insert( address(key) );
              signed_keys.insert( address(pts_address(key,false,56) ) );
              signed_keys.insert( address(pts_address(key,true,56) )  );
              signed_keys.insert( address(pts_address(key,false,0) )  );
              signed_keys.insert( address(pts_address(key,true,0) )   );
           }
        }
        for( auto op : trx.operations )
        {
           evaluate_operation( op );
        }
        post_evaluate();
        validate_required_fee();
        update_delegate_votes();
      } 
      catch ( const fc::exception& e )
      {
         validation_error = e;
         throw;
      }
   } FC_RETHROW_EXCEPTIONS( warn, "", ("trx",trx_arg) ) }

   void transaction_evaluation_state::evaluate_operation( const operation& op )
   {
      operation_factory::instance().evaluate( *this, op );
   }

   void transaction_evaluation_state::adjust_vote( slate_id_type slate_id, share_type amount )
   {
      if( slate_id )
      {
         auto slate = _current_state->get_delegate_slate( slate_id );
         if( !slate ) FC_CAPTURE_AND_THROW( unknown_delegate_slate, (slate_id) );
         for( auto delegate_id : slate->supported_delegates )
         {
#if BTS_BLOCKCHAIN_VERSION > 105 
            if( delegate_id < signed_int(0) )
               net_delegate_votes[abs(delegate_id)].votes_for -= amount;
            else
               net_delegate_votes[abs(delegate_id)].votes_for += amount;
#warning [HARDFORK] Remove below
#else
               net_delegate_votes[delegate_id].votes_for += amount;
#endif
         }
      }
   }

   share_type transaction_evaluation_state::get_fees( asset_id_type id )const
   {
      auto itr = balance.find(id);
      if( itr != balance.end() )
         return itr->second;
      return 0;
   }

   /**
    *  
    */
   void transaction_evaluation_state::sub_balance( const balance_id_type& balance_id, const asset& amount )
   {
      auto provided_deposit_itr = provided_deposits.find( balance_id );
      if( provided_deposit_itr == provided_deposits.end() )
      {
         provided_deposits[balance_id] = amount;
      }
      else
      {
         provided_deposit_itr->second += amount;
      }

      auto deposit_itr = deposits.find(amount.asset_id);
      if( deposit_itr == deposits.end()  ) 
      {
         deposits[amount.asset_id] = amount;
      }
      else
      {
         deposit_itr->second += amount;
      }

      auto balance_itr = balance.find(amount.asset_id);
      if( balance_itr != balance.end()  )
      {
          balance_itr->second -= amount.amount;
      }
      else
      {
          balance[amount.asset_id] =  -amount.amount;
      }
   }

   void transaction_evaluation_state::add_balance( const asset& amount )
   {
      auto withdraw_itr = withdraws.find( amount.asset_id );
      if( withdraw_itr == withdraws.end() )
         withdraws[amount.asset_id] = amount;
      else
         withdraw_itr->second += amount;

      auto balance_itr = balance.find( amount.asset_id );
      if( balance_itr == balance.end() )
         balance[amount.asset_id] = amount.amount;
      else
         balance_itr->second += amount.amount;
   }

   /**
    *  Throws if the asset is not known to the blockchain.
    */
   void transaction_evaluation_state::validate_asset( const asset& asset_to_validate )const
   {
      auto asset_rec = _current_state->get_asset_record( asset_to_validate.asset_id );
      if( NOT asset_rec ) 
         FC_CAPTURE_AND_THROW( unknown_asset_id, (asset_to_validate) );
   }

} } // bts::blockchain
