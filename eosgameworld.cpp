#include "eosgameworld.hpp"

void eosgameworld::transfer(account_name from, account_name to, asset quantity, string memo) {
    if (from == _self || to != _self) {
        return;
    }

    eosio_assert(quantity.symbol == S(4,EOS), "eosgameworld only accepts EOS");
    eosio_assert(quantity.is_valid(), "Invalid token transfer");
    eosio_assert(quantity.amount > 0, "Quantity must be positive");

    // if transfer amount is 0.0001 EOS then withdraw
    if (quantity.amount == 1) {
        withdraw(from, quantity);
        return;
    }

    eosio_assert(quantity.amount >= 1000, "最少购买0.1EOS");

    memo.erase(memo.begin(), find_if(memo.begin(), memo.end(), [](int ch) {
        return !isspace(ch);
    }));

    memo.erase(find_if(memo.rbegin(), memo.rend(), [](int ch) {
        return !isspace(ch);
    }).base(), memo.end());

    auto separator_pos = memo.find(' ');

    // valid reffer account
    account_name refer_account = 0;
    if (memo.length() > 0 && separator_pos == string::npos) {

        string refer_account_name_str = memo.substr(0, separator_pos);
        eosio_assert(refer_account_name_str.length() <= 12, "account name can only be 12 chars long");
        refer_account = string_to_name(refer_account_name_str.c_str());
        tb_player refer_player_sgt(_self, refer_account);
        if (!refer_player_sgt.exists()) {
            refer_account = 0;
        }
    }

    st_round round = get_round();

    eosio_assert((time_point_sec(now()) < round.end) && !round.ended, "this round is ended");
    eosio_assert(time_point_sec(now()) > round.start, "this round is not started, ");

    // dev fee
    uint64_t contract_fee = 2 * quantity.amount / 100;
    uint64_t contract_partner_fee = 2 * quantity.amount / 100;

    // refer fee
    uint64_t refer_fee = 11 * quantity.amount / 100;
    uint64_t refer_second_fee = 5 * quantity.amount / 100;

    // buy key
    st_player default_player = st_player{
        .key = 0,
        .eos = 0,
        .mask = 0,
        .affiliate_name = refer_account,
        .aff_vault = 0,
        .pot_vault = 0,
        .lottery_vault = 0,
    };
    tb_player players(_self, from);
    st_player player = players.get_or_create(from, default_player);

    uint64_t keys = buy_keys(quantity.amount);

    uint64_t min_key_to_buy = max(round.key / 10000, key_precision * 100);
    eosio_assert(keys >= min_key_to_buy, "amount of key should be bigger than 100 and one ten thousandths of keys in this round");

    player.eos += quantity.amount;
    player.key += keys;

    round.player = from;
    round.eos += quantity.amount;
    round.key += keys;
    eosio_assert(round.key >= keys, "amount of key overflow");

    // game time continue reduce  until latest 30 seconds
    if (time_point_sec(now() + 30) < round.end) {
        round.end = max(round.end - gap_delta, time_point_sec(now()));
    }

    // profit
    uint64_t base_profit = quantity.amount * PPOFIT / 100;
    uint64_t profit_per_key = base_profit * base / round.key;
    round.mask += profit_per_key;
    eosio_assert(round.mask >= profit_per_key, "mask overflow");

    // lottery
    round.draws += 1;

    // rate setting
    uint64_t key_rate = (player.eos / 1000) * 30 / 100;
    if (key_rate > 30) {
        key_rate = 30;
    }

    uint64_t draw_rate = round.draws * 70 / 100;

    uint64_t begin = key_rate + draw_rate;
    if (begin > 100) {
        begin = 100;
    }
    uint64_t roll_rate = rand(begin, 100);

    if (roll_rate >= 99) {
        // 中奖
        uint64_t vault = round.pot * 5 / 100;
        round.lottery += vault;
        round.pot = round.pot - vault;

        eosio_assert(vault < round.eos, "amount of lottery should be less than eos of this round");

        round.lottery = 0;
        round.draws = 0;

        // record player lottery
        player.lottery_vault += vault;


        asset vault_asset(vault, S(4,EOS));
        action(
                permission_level{ _self, N(active) },
                N(eosio.token),
                N(transfer),
                make_tuple(_self, from, vault_asset, string("eosgameworld lottery"))
        ).send();
    }

    uint64_t player_profit = profit_per_key * keys / base;
    player.mask += round.mask * keys / base - player_profit;

    uint64_t total_profit = profit_per_key * round.key / base;
    eosio_assert(total_profit <= base_profit, "final result of total profit shouldn't be bigger than base profit");

    uint64_t total_pot = quantity.amount - contract_fee - refer_fee - total_profit;
    eosio_assert(total_pot >= quantity.amount * (100 - PPOFIT - LOTTERY - 2 - 8) / 100, "something wrong with final result of total pot");

    round.pot += total_pot;
    eosio_assert(round.pot >= total_pot, "pot oeverflow");

    // save player and round
    players.set(player, from);
    sgt_round.set(round, _self);

    // refer fee
    if (player.affiliate_name != 0) {
        tb_player refer_player_sgt(_self, player.affiliate_name);
        eosio_assert(refer_player_sgt.exists(), "refer player not exist");

        st_player affilicate_player = refer_player_sgt.get();
        affilicate_player.aff_vault += refer_fee;
        eosio_assert(affilicate_player.aff_vault >= refer_fee, "affilicate fee overflow");

        refer_player_sgt.set(affilicate_player, player.affiliate_name);

        // second fee
        if (affilicate_player.affiliate_name != 0) {
            tb_player refer_second_player_sgt(_self, affilicate_player.affiliate_name);
            eosio_assert(refer_second_player_sgt.exists(), "refer second player not exist");

            st_player affilicate_second_player = refer_second_player_sgt.get();
            affilicate_second_player.aff_vault += refer_second_fee;
            eosio_assert(affilicate_second_player.aff_vault >= refer_second_fee, "affilicate second fee overflow");

            refer_second_player_sgt.set(affilicate_second_player, affilicate_player.affiliate_name);

        } else {
            contract_fee += refer_second_fee;
        }
    } else {
        contract_fee += refer_fee;
        contract_fee += refer_second_fee;
    }

    // contract fee
    asset contract_fee_asset(contract_fee, S(4,EOS));
    action(
            permission_level{ _self, N(active) },
            N(eosio.token),
            N(transfer),
            make_tuple(_self, contract_fee_account, contract_fee_asset, string(""))
    ).send();

    // contract partner fee
    asset contract_partner_fee_asset(contract_partner_fee, S(4,EOS));
    action(
            permission_level{ _self, N(active) },
            N(eosio.token),
            N(transfer),
            make_tuple(_self, contract_partner_fee_account, contract_partner_fee_asset, string(""))
    ).send();
}

void eosgameworld::withdraw(account_name to, asset quantity) {
    eosio_assert(has_auth(to) || has_auth(_self), "invalid auth");
    st_round round = get_round();

    if (time_point_sec(now()) > round.end && !round.ended) {
        round.ended = true;

        uint64_t win =  round.pot;
        sgt_round.set(round, _self);

        tb_player sgt_winner(_self, round.player);
        eosio_assert(sgt_winner.exists(), "winner not exist");

        if (sgt_winner.exists()) {
            st_player winner = sgt_winner.get();
            winner.pot_vault += win;
            sgt_winner.set(winner, round.player);
        }
    }

    // cal profit
    tb_player sgt_player(_self, to);
    eosio_assert(sgt_player.exists(), "player not exists");
    st_player player = sgt_player.get();
    uint64_t profit = round.mask * player.key / base - player.mask;
    if (profit > 0) {
        player.mask += profit;
    }
    uint64_t vault = profit + player.aff_vault + player.pot_vault;

    if (round.ended) {
        sgt_player.remove();
    } else {
        player.aff_vault = 0;
        player.pot_vault = 0;
        sgt_player.set(player, to);
    }
    eosio_assert(vault < round.eos, "amount of withdraw should be less than eos of this round");

    // set quantity
    eosio_assert(quantity.amount + vault > quantity.amount,
                 "integer overflow adding withdraw balance");

    vault += quantity.amount;

    // transfer
    if (vault > 0) {
        print(" vault:", vault);
        asset vault_asset(vault, S(4,EOS));
        action(
                permission_level{ _self, N(active) },
                N(eosio.token),
                N(transfer),
                make_tuple(_self, to, vault_asset, string("eosgameworld withdraw"))
        ).send();
    }
}

void eosgameworld::create(time_point_sec start) {
    require_auth(_self);
    eosio_assert(time_point_sec(now()) < start, "invalid start time");

    eosio_assert(!sgt_round.exists() || (sgt_round.get().end < time_point_sec(now())), "not the time to create new round");

    st_round round = st_round{
            .eos = 0,
            .pot = 0,
            .mask = 0,
            .key = 0,
            .lottery = 0,
            .draws = 0,
            .end = time_point_sec(start + gap),
            .ended = false,
            .player = _self,
            .start = start,
    };
    sgt_round.set(round, _self);
}

void eosgameworld::test() {
    print("hello: 123");
}