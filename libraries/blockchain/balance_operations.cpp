#include <bts/blockchain/balance_operations.hpp>
#include <bts/blockchain/chain_interface.hpp>
#include <bts/blockchain/exceptions.hpp>

namespace bts { namespace blockchain {

   balance_id_type  deposit_operation::balance_id()const
   {
      return condition.get_address();
   }

   deposit_operation::deposit_operation( const address& owner, 
                                         const asset& amnt, 
                                         slate_id_type slate_id )
   {
      FC_ASSERT( amnt.amount > 0 );
      amount = amnt.amount;
      condition = withdraw_condition( withdraw_with_signature( owner ), 
                                      amnt.asset_id, slate_id );
   }

   void define_delegate_slate_operation::evaluate( transaction_evaluation_state& eval_state )
   { try {
      auto slate_id = this->slate.id();

      if( this->slate.supported_delegates.size() > BTS_BLOCKCHAIN_MAX_SLATE_SIZE )
         FC_CAPTURE_AND_THROW( too_may_delegates_in_slate, (slate.supported_delegates.size()) );

      auto current_slate = eval_state._current_state->get_delegate_slate( slate_id );
      if( NOT current_slate ) 
      {
         for( auto delegate_id : this->slate.supported_delegates )
         {
#if BTS_BLOCKCHAIN_VERSION > 105 
            eval_state.verify_delegate_id( abs(delegate_id) );
#warning [HARDFORK] Remove below
#else
            eval_state.verify_delegate_id( delegate_id );
#endif
         }
         eval_state._current_state->store_delegate_slate( slate_id, slate );
      }
   } FC_CAPTURE_AND_RETHROW( (eval_state) ) }

   /**
    *  TODO: Document Rules for Deposits
    *
    */
   void deposit_operation::evaluate( transaction_evaluation_state& eval_state )
   { try {
       if( this->amount <= 0 ) 
          FC_CAPTURE_AND_THROW( negative_deposit, (amount) );

       auto deposit_balance_id = this->balance_id();

       auto cur_record = eval_state._current_state->get_balance_record( deposit_balance_id );
       if( !cur_record )
       {
          cur_record = balance_record( this->condition );
       }
       cur_record->last_update   = eval_state._current_state->now();
       cur_record->balance       += this->amount;

       eval_state.sub_balance( deposit_balance_id, asset(this->amount, cur_record->condition.asset_id) );
       
       if( cur_record->condition.asset_id == 0 && cur_record->condition.delegate_slate_id ) 
          eval_state.adjust_vote( cur_record->condition.delegate_slate_id, this->amount );

       eval_state._current_state->store_balance_record( *cur_record );
   } FC_CAPTURE_AND_RETHROW( (*this) ) }

   /**
    *  TODO: Document rules for Withdraws
    */
   void withdraw_operation::evaluate( transaction_evaluation_state& eval_state )
   { try {
       if( this->amount <= 0 ) 
          FC_CAPTURE_AND_THROW( negative_deposit, (amount) );
       
      obalance_record current_balance_record = eval_state._current_state->get_balance_record( this->balance_id );

      if( !current_balance_record ) 
         FC_CAPTURE_AND_THROW( unknown_balance_record, (balance_id) );

      if( this->amount > current_balance_record->balance ) 
         FC_CAPTURE_AND_THROW( insufficient_funds, 
                               (current_balance_record)
                               (amount)
                               (current_balance_record->balance - amount) );

      switch( (withdraw_condition_types)current_balance_record->condition.type )
      {
         case withdraw_signature_type:  
         {
            auto owner = current_balance_record->condition.as<withdraw_with_signature>().owner;
            if( !eval_state.check_signature( owner ) )
               FC_CAPTURE_AND_THROW( missing_signature, (owner) );
            break;
         }

         case withdraw_multi_sig_type:
         {
            auto multi_sig = current_balance_record->condition.as<withdraw_with_multi_sig>();
            uint32_t valid_signatures = 0;
            for( auto sig : multi_sig.owners )
               valid_signatures += eval_state.check_signature( sig );
            if( valid_signatures < multi_sig.required )
               FC_CAPTURE_AND_THROW( missing_signature, (valid_signatures)(multi_sig) );
            break;
         }

         case withdraw_password_type:
         {
            auto password_condition = current_balance_record->condition.as<withdraw_with_password>();
            try {
               if( password_condition.timeout < eval_state._current_state->now() )
               {
                  if( !eval_state.check_signature( password_condition.payor ) )
                     FC_CAPTURE_AND_THROW( missing_signature, (password_condition.payor) );
               }
               else 
               {
                  if( !eval_state.check_signature( password_condition.payee ) )
                     FC_CAPTURE_AND_THROW( missing_signature, (password_condition.payee) );
                  if( claim_input_data.size() < sizeof( fc::ripemd160 ) )
                     FC_CAPTURE_AND_THROW( invalid_claim_password, (claim_input_data) );

                  auto input_password_hash = fc::ripemd160::hash( claim_input_data.data(), 
                                                                  claim_input_data.size() );

                  if( password_condition.password_hash != input_password_hash )
                     FC_CAPTURE_AND_THROW( invalid_claim_password, (input_password_hash) );
               }
            } FC_CAPTURE_AND_RETHROW( (password_condition ) ) 
            break;
         }

         case withdraw_option_type:
         {
            auto option = current_balance_record->condition.as<withdraw_option>();
            try {
               if( eval_state._current_state->now() > option.date )
               {
                  if( !eval_state.check_signature( option.optionor ) )
                     FC_CAPTURE_AND_THROW( missing_signature, (option.optionor) );
               }
               else // the option hasn't expired
               {
                  if( !eval_state.check_signature( option.optionee ) )
                     FC_CAPTURE_AND_THROW( missing_signature, (option.optionee) );

                  auto pay_amount = asset( this->amount, current_balance_record->condition.asset_id ) * option.strike_price;
                  eval_state.add_required_deposit( option.optionee, pay_amount ); 
               }
            } FC_CAPTURE_AND_RETHROW( (option) )
            break;
         }
         case withdraw_null_type:
            FC_CAPTURE_AND_THROW( invalid_withdraw_condition, (current_balance_record->condition) );
            break;
      //   default:
      }
      // update delegate vote on withdrawn account..

      current_balance_record->balance -= this->amount;
      current_balance_record->last_update = eval_state._current_state->now();
      eval_state.add_balance( asset(this->amount, current_balance_record->condition.asset_id) );

      if( current_balance_record->condition.asset_id == 0 && current_balance_record->condition.delegate_slate_id ) 
         eval_state.adjust_vote( current_balance_record->condition.delegate_slate_id, -this->amount );

      eval_state._current_state->store_balance_record( *current_balance_record );
   } FC_CAPTURE_AND_RETHROW( (*this) ) }

} } // bts::blockchain
