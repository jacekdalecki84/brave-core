/* Copyright (c) 2019 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "bat/ledger/internal/wallet/balance.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "base/json/json_reader.h"
#include "bat/ledger/internal/bat_helper.h"
#include "bat/ledger/internal/ledger_impl.h"
#include "bat/ledger/internal/rapidjson_bat_helper.h"
#include "net/http/http_status_code.h"

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;

namespace braveledger_wallet {

Balance::Balance(bat_ledger::LedgerImpl* ledger) : ledger_(ledger) {
}

Balance::~Balance() {
}

void Balance::Fetch(ledger::FetchBalanceCallback callback) {
  std::string payment_id = ledger_->GetPaymentId();

  std::string path = (std::string)WALLET_PROPERTIES
      + payment_id
      + WALLET_PROPERTIES_END;
  const std::string url = braveledger_bat_helper::buildURL(
      path,
      PREFIX_V2,
      braveledger_bat_helper::SERVER_TYPES::BALANCE);
  auto load_callback = std::bind(&Balance::OnWalletProperties,
                            this,
                            _1,
                            _2,
                            _3,
                            callback);
  ledger_->LoadURL(url,
                   std::vector<std::string>(),
                   std::string(),
                   std::string(),
                   ledger::URL_METHOD::GET,
                   load_callback);
}

void Balance::OnWalletProperties(
    int response_status_code,
    const std::string& response,
    const std::map<std::string, std::string>& headers,
    ledger::FetchBalanceCallback callback) {
  ledger::BalancePtr balance = ledger::Balance::New();
  ledger_->LogResponse(__func__, response_status_code, response, headers);
  if (response_status_code != net::HTTP_OK) {
    callback(ledger::Result::LEDGER_ERROR, std::move(balance));
    return;
  }

  base::Optional<base::Value> value = base::JSONReader::Read(response);
  if (!value || !value->is_dict()) {
    callback(ledger::Result::LEDGER_ERROR, std::move(balance));
    return;
  }

  base::DictionaryValue* dictionary = nullptr;
  if (!value->GetAsDictionary(&dictionary)) {
    callback(ledger::Result::LEDGER_ERROR, std::move(balance));
    return;
  }

  auto* total = dictionary->FindKey("balance");
  double total_anon = 0.0;
  if (total) {
    total_anon = std::stod(total->GetString());
  }
  balance->total = total_anon;

  auto* local_rates = dictionary->FindKey("rates");
  if (local_rates) {
    base::DictionaryValue* dict_value = nullptr;
    if (local_rates->GetAsDictionary(&dict_value)) {
      base::DictionaryValue::Iterator iter(*dict_value);
      while (!iter.IsAtEnd()) {
        balance->rates.insert(
            std::make_pair(iter.key(), iter.value().GetDouble()));
        iter.Advance();
      }
    }
  }

  balance->wallets.insert(std::make_pair(ledger::kWalletAnonymous, total_anon));

  callback(ledger::Result::LEDGER_OK, std::move(balance));
}

}  // namespace braveledger_wallet
