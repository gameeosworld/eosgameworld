#include <eosiolib/eosio.hpp>
#include <eosiolib/asset.hpp>
#include <eosiolib/types.hpp>
#include <eosiolib/action.hpp>
#include <eosiolib/symbol.hpp>
#include <eosiolib/time.hpp>
#include <eosiolib/singleton.hpp>
#include <math.h>
#include <eosiolib/transaction.hpp>
#include <eosiolib/crypto.h>

using namespace eosio;
using namespace std;

const uint32_t gap = 6 * 60 * 60;
const uint32_t gap_delta = 30;
const uint64_t base = 1000ll * 1000;
const account_name contract_fee_account = N(eosgametax11);
const account_name contract_partner_fee_account = N(eosgametax11);

const uint64_t key_precision = 100;

const uint8_t PPOFIT = 55;
const uint8_t POT = 25;
const uint8_t LOTTERY = 5;

class eosgameworld: public contract {
public:
    eosgameworld(account_name self)
            : contract(self),
              sgt_round(_self, _self)
    {};

    void transfer(account_name from, account_name to, asset quantity, string memo);

    void withdraw(account_name to, asset quantity);

    // @abi action
    void create(time_point_sec start);

    // @abi action
    void test();
private:

    // @abi table round i64
    struct st_round {
        account_name player;
        bool ended;
        time_point_sec end;
        uint64_t key;
        uint64_t eos;
        uint64_t pot;
        uint64_t mask;
        uint64_t lottery; // 抽奖中奖累计
        uint64_t draws; // 抽奖计数
        time_point_sec start;
    };
    typedef singleton<N(round), st_round> tb_round;
    tb_round sgt_round;

    // @abi table player i64
    struct st_player {
        account_name affiliate_name;
        uint64_t aff_vault;
        uint64_t pot_vault;
        uint64_t lottery_vault; // 中奖
        uint64_t key;
        uint64_t eos;
        uint64_t mask;
    };
    typedef singleton<N(player), st_player> tb_player;

    uint64_t buy_keys(uint64_t eos) {
        st_round round = get_round();
        return key(round.eos + eos) - key(round.eos);
    };

    uint64_t key(uint64_t eos) {
        return key_precision * (sqrt(eos * 1280000 + 230399520000) - 479999);
    }
    st_round get_round() {
        eosio_assert(sgt_round.exists(), "round not exist");
        return sgt_round.get();
    }
    uint64_t rand(uint64_t min, uint64_t max) {
        checksum256 result;
        auto mixedBlock = tapos_block_prefix() * tapos_block_num();

        const char *mixedChar = reinterpret_cast<const char *>(&mixedBlock);
        sha256( (char *)mixedChar, sizeof(mixedChar), &result);
        const char *p64 = reinterpret_cast<const char *>(&result);

        uint64_t number = (abs((int64_t)p64[0]) % (max + 1 - min)) + min;
        return number;
    }

};

#define EOSIO_ABI_EX( TYPE, MEMBERS ) \
extern "C" { \
    void apply( uint64_t receiver, uint64_t code, uint64_t action ) { \
        auto self = receiver; \
        if( action == N(onerror)) { \
            /* onerror is only valid if it is for the "eosio" code account and authorized by "eosio"'s "active permission */ \
            eosio_assert(code == N(eosio), "onerror action's are only valid from the \"eosio\" system account"); \
        } \
        if( ((code == self && action != N(transfer)) || action == N(onerror)) || (code == N(eosio.token) && action == N(transfer)) ) { \
            TYPE thiscontract( self ); \
            switch( action ) { \
                EOSIO_API( TYPE, MEMBERS ) \
            } \
         /* does not allow destructor of thiscontract to run: eosio_exit(0); */ \
        } \
    } \
} \

EOSIO_ABI_EX(eosgameworld, (withdraw) (transfer) (create) (test))