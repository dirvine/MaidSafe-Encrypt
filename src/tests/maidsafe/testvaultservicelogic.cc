/*
* ============================================================================
*
* Copyright [2010] maidsafe.net limited
*
* Description:  Test for VaultServiceLogic class using mock RPCs
* Version:      1.0
* Created:      2010-01-08-12.33.18
* Revision:     none
* Compiler:     gcc
* Author:       Fraser Hutchison (fh), fraser.hutchison@maidsafe.net
* Company:      maidsafe.net limited
*
* The following source code is property of maidsafe.net limited and is not
* meant for external use.  The use of this code is governed by the license
* file LICENSE.TXT found in the root of this directory and also on
* www.maidsafe.net.
*
* You are not free to copy, amend or otherwise use this source code without
* the explicit written permission of the board of directors of maidsafe.net.
*
* ============================================================================
*/

#include "tests/maidsafe/mockvaultservicelogic.h"
#include "tests/maidsafe/mockkadops.h"

namespace maidsafe_vault {

class VaultServiceLogicTest : public MockVaultServiceLogicTest {};

TEST_F(VaultServiceLogicTest, BEH_MAID_VSL_Offline) {
  boost::shared_ptr<MockVaultRpcs> mock_rpcs(new MockVaultRpcs(NULL, NULL));
  VaultServiceLogic vsl(mock_rpcs, boost::shared_ptr<kad::KNode>());

  maidsafe::AddToReferenceListRequest arr;
  boost::mutex mutex;
  boost::condition_variable cv;
  int result(kGeneralError);
  VoidFuncOneInt cb1 =
      boost::bind(&mock_vsl::CopyResult, _1, &mutex, &cv, &result);
  vsl.AddToRemoteRefList(arr, kSuccess, cb1, 0);
  {
    boost::mutex::scoped_lock lock(mutex);
    while (result == kGeneralError) {
      cv.wait(lock);
    }
  }
  ASSERT_EQ(kVaultOffline, result);

  result = kGeneralError;
  maidsafe::AmendAccountRequest aar;
  VoidFuncOneInt cb2 =
      boost::bind(&mock_vsl::CopyResult, _1, &mutex, &cv, &result);
  vsl.AmendRemoteAccount(aar, kSuccess, cb2, 0);
  {
    boost::mutex::scoped_lock lock(mutex);
    while (result == kGeneralError) {
      cv.wait(lock);
    }
  }
  ASSERT_EQ(kVaultOffline, result);

  result = kGeneralError;
  maidsafe::AccountStatusRequest asr;
  VoidFuncOneInt cb3 =
      boost::bind(&mock_vsl::CopyResult, _1, &mutex, &cv, &result);
  vsl.RemoteVaultAbleToStore(asr, kSuccess, cb3, 0);
  {
    boost::mutex::scoped_lock lock(mutex);
    while (result == kGeneralError) {
      cv.wait(lock);
    }
  }
  ASSERT_EQ(kVaultOffline, result);
}

TEST_F(VaultServiceLogicTest, FUNC_MAID_VSL_AddToRemoteRefList) {
  // Setup
  boost::shared_ptr<MockVaultRpcs> mock_rpcs(new MockVaultRpcs(NULL, NULL));
  MockVsl vsl(mock_rpcs, boost::shared_ptr<kad::KNode>());
  vsl.pmid_ = pmid_;
  vsl.pmid_public_signature_ = pmid_public_signature_;
  vsl.pmid_private_ = pmid_private_;
  vsl.online_ = true;
  vsl.our_details_ = our_contact_;

  std::vector<maidsafe::AddToReferenceListResponse> good_responses;
  std::vector<maidsafe::AddToReferenceListResponse> good_responses_less_one;
  for (size_t i = 0; i < good_contacts_.size(); ++i) {
    maidsafe::AddToReferenceListResponse add_ref_response;
    add_ref_response.set_result(kAck);
    add_ref_response.set_pmid(good_contacts_.at(i).node_id());
    good_responses.push_back(add_ref_response);
    if (i < good_contacts_.size() - 1)
      good_responses_less_one.push_back(add_ref_response);
  }
  std::vector<maidsafe::AddToReferenceListResponse>
      bad_pmid_responses(good_responses);
  std::vector<maidsafe::AddToReferenceListResponse>
      too_few_ack_responses(good_responses);
  std::vector<maidsafe::AddToReferenceListResponse>
      fail_initialise_responses(good_responses);
  for (size_t i = kKadUpperThreshold - 1; i < good_contacts_.size(); ++i) {
    bad_pmid_responses.at(i).set_pmid(good_contacts_.at(i - 1).node_id());
    too_few_ack_responses.at(i).set_result(kNack);
    fail_initialise_responses.at(i).clear_result();
  }
  // Set chunkname as far as possible from our ID so we don't get added to
  // vector of close nodes in vsl.HandleFindKNodesResponse
  std::string far_chunkname =
      crypto_.Obfuscate(pmid_, std::string(64, -1), crypto::XOR);

  maidsafe::AddToReferenceListRequest request, close_request;
  request.set_chunkname(far_chunkname);
  close_request.set_chunkname(pmid_);
  request.mutable_store_contract();
  close_request.mutable_store_contract();
  boost::mutex mutex;
  boost::condition_variable cv;
  int result(kGeneralError);
  VoidFuncOneInt cb =
      boost::bind(&mock_vsl::CopyResult, _1, &mutex, &cv, &result);

  // Expectations
  EXPECT_CALL(vsl, AddToRemoteRefList(testing::_, testing::_, testing::_,
                                      testing::_))
      .WillRepeatedly(testing::Invoke(&vsl, &MockVsl::AddToRemoteRefListReal));
  EXPECT_CALL(*vsl.kadops(), FindCloseNodes(far_chunkname,
      testing::An<const base::callback_func_type&>()))
      .WillOnce(testing::WithArgs<1>(testing::Invoke(
          boost::bind(&mock_kadops::RunCallback, fail_parse_result_, _1))))
      .WillOnce(testing::WithArgs<1>(testing::Invoke(
          boost::bind(&mock_kadops::RunCallback, fail_result_, _1))))  // Call 2
      .WillOnce(testing::WithArgs<1>(testing::Invoke(
          boost::bind(&mock_kadops::RunCallback, few_result_, _1))))  // Call 3
      .WillOnce(testing::WithArgs<1>(testing::Invoke(
          boost::bind(&mock_kadops::RunCallback, good_result_, _1))))  // Call 4
      .WillOnce(testing::WithArgs<1>(testing::Invoke(
          boost::bind(&mock_kadops::RunCallback, good_result_, _1))))  // Call 6
      .WillOnce(testing::WithArgs<1>(testing::Invoke(
          boost::bind(&mock_kadops::RunCallback, good_result_, _1))))  // Call 7
      .WillOnce(testing::WithArgs<1>(testing::Invoke(
          boost::bind(&mock_kadops::RunCallback, good_result_, _1))));  // 8
  EXPECT_CALL(*vsl.kadops(), FindCloseNodes(pmid_,
      testing::An<const base::callback_func_type&>()))  // Call 5
      .WillOnce(testing::WithArgs<1>(testing::Invoke(
          boost::bind(&mock_kadops::RunCallback, good_result_less_one_, _1))));

  for (size_t i = 0; i < good_contacts_.size(); ++i) {
    if (i < good_contacts_.size() - 1) {
      EXPECT_CALL(*vsl.kadops(), AddressIsLocal(good_contacts_.at(i)))
          .WillOnce(testing::Return(true))  // Call 4
          .WillOnce(testing::Return(false))  // Call 5
          .WillOnce(testing::Return(true))  // Call 6
          .WillOnce(testing::Return(true))  // Call 7
          .WillOnce(testing::Return(true));  // Call 8
    } else {
      EXPECT_CALL(*vsl.kadops(), AddressIsLocal(good_contacts_.at(i)))
          .WillOnce(testing::Return(true))  // Call 4
          .WillOnce(testing::Return(true))  // Call 6
          .WillOnce(testing::Return(true))  // Call 7
          .WillOnce(testing::Return(true));  // Call 8
    }
  }

  for (size_t i = 0; i < good_contacts_.size(); ++i) {
      EXPECT_CALL(*mock_rpcs, AddToReferenceList(good_contacts_.at(i), true, 0,
          testing::_, testing::_, testing::_, testing::_))
              .WillOnce(DoAll(testing::SetArgumentPointee<4>(
                                    good_responses.at(i)),  // Call 4
                              testing::WithArgs<6>(testing::Invoke(
                  boost::bind(&mock_vsl::ThreadedDoneRun, 100, 5000, _1)))))
              .WillOnce(DoAll(testing::SetArgumentPointee<4>(
                                    bad_pmid_responses.at(i)),  // Call 6
                              testing::WithArgs<6>(testing::Invoke(
                  boost::bind(&mock_vsl::ThreadedDoneRun, 100, 5000, _1)))))
              .WillOnce(DoAll(testing::SetArgumentPointee<4>(
                                    too_few_ack_responses.at(i)),  // Call 7
                              testing::WithArgs<6>(testing::Invoke(
                  boost::bind(&mock_vsl::ThreadedDoneRun, 100, 5000, _1)))))
              .WillOnce(DoAll(testing::SetArgumentPointee<4>(
                                    fail_initialise_responses.at(i)),  // Call 8
                              testing::WithArgs<6>(testing::Invoke(
                  boost::bind(&mock_vsl::ThreadedDoneRun, 100, 5000, _1)))));
  }
  for (size_t i = 0; i < good_contacts_.size() - 1; ++i) {
      EXPECT_CALL(*mock_rpcs, AddToReferenceList(good_contacts_.at(i), false, 0,
          testing::_, testing::_, testing::_, testing::_))  // Call 5
              .WillOnce(DoAll(testing::SetArgumentPointee<4>(
                                    good_responses_less_one.at(i)),
                              testing::WithArgs<6>(testing::Invoke(
                  boost::bind(&mock_vsl::ThreadedDoneRun, 100, 5000, _1)))));
  }

  // Call 1 - FindKNodes fails (NULL pointer)
  vsl.AddToRemoteRefList(request, kSuccess, cb, 0);
  {
    boost::mutex::scoped_lock lock(mutex);
    while (result == kGeneralError) {
      cv.wait(lock);
    }
  }
  ASSERT_EQ(kVaultServiceFindNodesError, result);

  // Call 2 - FindKNodes returns kNack
  result = kGeneralError;
  vsl.AddToRemoteRefList(request, kSuccess, cb, 0);
  {
    boost::mutex::scoped_lock lock(mutex);
    while (result == kGeneralError) {
      cv.wait(lock);
    }
  }
  ASSERT_EQ(kVaultServiceFindNodesFailure, result);

  // Call 3 - FindKnodes only returns 1 node
  result = kGeneralError;
  vsl.AddToRemoteRefList(request, kSuccess, cb, 0);
  {
    boost::mutex::scoped_lock lock(mutex);
    while (result == kGeneralError) {
      cv.wait(lock);
    }
  }
  ASSERT_EQ(kVaultServiceFindNodesTooFew, result);

  // Call 4 - All OK
  result = kGeneralError;
  vsl.AddToRemoteRefList(request, kSuccess, cb, 0);
  {
    boost::mutex::scoped_lock lock(mutex);
    while (result == kGeneralError) {
      cv.wait(lock);
    }
  }
  ASSERT_EQ(kSuccess, result);

  // Call 5 - All OK - we're close to chunkname, so we replace contact 16
  result = kGeneralError;
  vsl.AddToRemoteRefList(close_request, kSuccess, cb, 0);
  {
    boost::mutex::scoped_lock lock(mutex);
    while (result == kGeneralError) {
      cv.wait(lock);
    }
  }
  ASSERT_EQ(kSuccess, result);

  // Call 6 - Five responses have incorrect PMID
  result = kGeneralError;
  vsl.AddToRemoteRefList(request, kSuccess, cb, 0);
  {
    boost::mutex::scoped_lock lock(mutex);
    while (result == kGeneralError) {
      cv.wait(lock);
    }
  }
  ASSERT_EQ(kRemoteOpResponseError, result);

  // Call 7 - Five responses return kNack
  result = kGeneralError;
  vsl.AddToRemoteRefList(request, kSuccess, cb, 0);
  {
    boost::mutex::scoped_lock lock(mutex);
    while (result == kGeneralError) {
      cv.wait(lock);
    }
  }
  ASSERT_EQ(kRemoteOpResponseFailed, result);

  // Call 8 - Five responses don't have result set
  result = kGeneralError;
  vsl.AddToRemoteRefList(request, kSuccess, cb, 0);
  {
    boost::mutex::scoped_lock lock(mutex);
    while (result == kGeneralError) {
      cv.wait(lock);
    }
  }
  ASSERT_EQ(kRemoteOpResponseUninitialised, result);
}

TEST_F(VaultServiceLogicTest, FUNC_MAID_VSL_AmendRemoteAccount) {
  // Setup
  boost::shared_ptr<MockVaultRpcs> mock_rpcs(new MockVaultRpcs(NULL, NULL));
  MockVsl vsl(mock_rpcs, boost::shared_ptr<kad::KNode>());
  vsl.pmid_ = pmid_;
  vsl.pmid_public_signature_ = pmid_public_signature_;
  vsl.pmid_private_ = pmid_private_;
  vsl.online_ = true;
  vsl.our_details_ = our_contact_;

  std::vector<maidsafe::AmendAccountResponse> good_responses;
  std::vector<maidsafe::AmendAccountResponse> good_responses_less_one;
  for (size_t i = 0; i < good_contacts_.size(); ++i) {
    maidsafe::AmendAccountResponse amend_acc_response;
    amend_acc_response.set_result(kAck);
    amend_acc_response.set_pmid(good_contacts_.at(i).node_id());
    good_responses.push_back(amend_acc_response);
    if (i < good_contacts_.size() - 1)
      good_responses_less_one.push_back(amend_acc_response);
  }
  std::vector<maidsafe::AmendAccountResponse>
      bad_pmid_responses(good_responses);
  std::vector<maidsafe::AmendAccountResponse>
      too_few_ack_responses(good_responses);
  std::vector<maidsafe::AmendAccountResponse>
      fail_initialise_responses(good_responses);
  for (size_t i = kKadUpperThreshold - 1; i < good_contacts_.size(); ++i) {
    bad_pmid_responses.at(i).set_pmid(good_contacts_.at(i - 1).node_id());
    too_few_ack_responses.at(i).set_result(kNack);
    fail_initialise_responses.at(i).clear_result();
  }

  std::string account_owner(crypto_.Hash("Account Owner", "",
      crypto::STRING_STRING, false));
  std::string account_name(crypto_.Hash(account_owner + kAccount, "",
      crypto::STRING_STRING, false));
  maidsafe::AmendAccountRequest request;
  request.set_amendment_type(maidsafe::AmendAccountRequest::kSpaceGivenInc);
  request.set_account_pmid(account_owner);
  maidsafe::SignedSize *mutable_signed_size = request.mutable_signed_size();
  mutable_signed_size->set_data_size(10);
  mutable_signed_size->set_pmid(pmid_);
  mutable_signed_size->set_signature(crypto_.AsymSign(base::itos_ull(10), "",
                                     pmid_private_, crypto::STRING_STRING));
  mutable_signed_size->set_public_key(pmid_public_);
  mutable_signed_size->set_public_key_signature(pmid_public_signature_);
  request.set_chunkname(crypto_.Hash("Chunkname", "", crypto::STRING_STRING,
      false));

  boost::mutex mutex;
  boost::condition_variable cv;
  int result(kGeneralError);
  VoidFuncOneInt cb =
      boost::bind(&mock_vsl::CopyResult, _1, &mutex, &cv, &result);

  // Expectations
  EXPECT_CALL(vsl, AmendRemoteAccount(testing::_, testing::_, testing::_,
                                      testing::_))
      .WillRepeatedly(testing::Invoke(&vsl, &MockVsl::AmendRemoteAccountReal));
  EXPECT_CALL(*vsl.kadops(), FindCloseNodes(account_name,
      testing::An<const base::callback_func_type&>()))
      .WillOnce(testing::WithArgs<1>(testing::Invoke(
          boost::bind(&mock_kadops::RunCallback, fail_parse_result_, _1))))
      .WillOnce(testing::WithArgs<1>(testing::Invoke(
          boost::bind(&mock_kadops::RunCallback, fail_result_, _1))))  // Call 2
      .WillOnce(testing::WithArgs<1>(testing::Invoke(
          boost::bind(&mock_kadops::RunCallback, few_result_, _1))))  // Call 3
      .WillOnce(testing::WithArgs<1>(testing::Invoke(
          boost::bind(&mock_kadops::RunCallback, good_result_, _1))))  // Call 4
      .WillOnce(testing::WithArgs<1>(testing::Invoke(
          boost::bind(&mock_kadops::RunCallback, good_result_less_one_, _1))))
      .WillOnce(testing::WithArgs<1>(testing::Invoke(
          boost::bind(&mock_kadops::RunCallback, good_result_, _1))))  // Call 6
      .WillOnce(testing::WithArgs<1>(testing::Invoke(
          boost::bind(&mock_kadops::RunCallback, good_result_, _1))))  // Call 7
      .WillOnce(testing::WithArgs<1>(testing::Invoke(
          boost::bind(&mock_kadops::RunCallback, good_result_, _1))));  // 8

  for (size_t i = 0; i < good_contacts_.size(); ++i) {
    if (i < good_contacts_.size() - 1) {
      EXPECT_CALL(*vsl.kadops(), AddressIsLocal(good_contacts_.at(i)))
          .WillOnce(testing::Return(true))  // Call 4
          .WillOnce(testing::Return(false))  // Call 5
          .WillOnce(testing::Return(true))  // Call 6
          .WillOnce(testing::Return(true))  // Call 7
          .WillOnce(testing::Return(true));  // Call 8
    } else {
      EXPECT_CALL(*vsl.kadops(), AddressIsLocal(good_contacts_.at(i)))
          .WillOnce(testing::Return(true))  // Call 4
          .WillOnce(testing::Return(true))  // Call 6
          .WillOnce(testing::Return(true))  // Call 7
          .WillOnce(testing::Return(true));  // Call 8
    }
  }

  for (size_t i = 0; i < good_contacts_.size(); ++i) {
      EXPECT_CALL(*mock_rpcs, AmendAccount(good_contacts_.at(i), true, 0,
          testing::_, testing::_, testing::_, testing::_))
              .WillOnce(DoAll(testing::SetArgumentPointee<4>(
                                    good_responses.at(i)),  // Call 4
                              testing::WithArgs<6>(testing::Invoke(
                  boost::bind(&mock_vsl::ThreadedDoneRun, 100, 5000, _1)))))
              .WillOnce(DoAll(testing::SetArgumentPointee<4>(
                                    bad_pmid_responses.at(i)),  // Call 6
                              testing::WithArgs<6>(testing::Invoke(
                  boost::bind(&mock_vsl::ThreadedDoneRun, 100, 5000, _1)))))
              .WillOnce(DoAll(testing::SetArgumentPointee<4>(
                                    too_few_ack_responses.at(i)),  // Call 7
                              testing::WithArgs<6>(testing::Invoke(
                  boost::bind(&mock_vsl::ThreadedDoneRun, 100, 5000, _1)))))
              .WillOnce(DoAll(testing::SetArgumentPointee<4>(
                                    fail_initialise_responses.at(i)),  // Call 8
                              testing::WithArgs<6>(testing::Invoke(
                  boost::bind(&mock_vsl::ThreadedDoneRun, 100, 5000, _1)))));
  }
  for (size_t i = 0; i < good_contacts_.size() - 1; ++i) {
      EXPECT_CALL(*mock_rpcs, AmendAccount(good_contacts_.at(i), false, 0,
          testing::_, testing::_, testing::_, testing::_))  // Call 5
              .WillOnce(DoAll(testing::SetArgumentPointee<4>(
                                    good_responses_less_one.at(i)),
                              testing::WithArgs<6>(testing::Invoke(
                  boost::bind(&mock_vsl::ThreadedDoneRun, 100, 5000, _1)))));
  }

  // Call 1 - FindKNodes fails (NULL pointer)
  vsl.AmendRemoteAccount(request, kSuccess, cb, 0);
  {
    boost::mutex::scoped_lock lock(mutex);
    while (result == kGeneralError) {
      cv.wait(lock);
    }
  }
  ASSERT_EQ(kVaultServiceFindNodesError, result);

  // Call 2 - FindKNodes returns kNack
  result = kGeneralError;
  vsl.AmendRemoteAccount(request, kSuccess, cb, 0);
  {
    boost::mutex::scoped_lock lock(mutex);
    while (result == kGeneralError) {
      cv.wait(lock);
    }
  }
  ASSERT_EQ(kVaultServiceFindNodesFailure, result);

  // Call 3 - FindKnodes only returns 1 node
  result = kGeneralError;
  vsl.AmendRemoteAccount(request, kSuccess, cb, 0);
  {
    boost::mutex::scoped_lock lock(mutex);
    while (result == kGeneralError) {
      cv.wait(lock);
    }
  }
  ASSERT_EQ(kVaultServiceFindNodesTooFew, result);

  // Call 4 - All OK
  result = kGeneralError;
  vsl.AmendRemoteAccount(request, kSuccess, cb, 0);
  {
    boost::mutex::scoped_lock lock(mutex);
    while (result == kGeneralError) {
      cv.wait(lock);
    }
  }
  ASSERT_EQ(kSuccess, result);

  // Call 5 - All OK - we're close to chunkname, so we replace contact 16
  result = kGeneralError;
  vsl.AmendRemoteAccount(request, kSuccess, cb, 0);
  {
    boost::mutex::scoped_lock lock(mutex);
    while (result == kGeneralError) {
      cv.wait(lock);
    }
  }
  ASSERT_EQ(kSuccess, result);

  // Call 6 - Five responses have incorrect PMID
  result = kGeneralError;
  vsl.AmendRemoteAccount(request, kSuccess, cb, 0);
  {
    boost::mutex::scoped_lock lock(mutex);
    while (result == kGeneralError) {
      cv.wait(lock);
    }
  }
  ASSERT_EQ(kRemoteOpResponseError, result);

  // Call 7 - Five responses return kNack
  result = kGeneralError;
  vsl.AmendRemoteAccount(request, kSuccess, cb, 0);
  {
    boost::mutex::scoped_lock lock(mutex);
    while (result == kGeneralError) {
      cv.wait(lock);
    }
  }
  ASSERT_EQ(kRemoteOpResponseFailed, result);

  // Call 8 - Five responses don't have result set
  result = kGeneralError;
  vsl.AmendRemoteAccount(request, kSuccess, cb, 0);
  {
    boost::mutex::scoped_lock lock(mutex);
    while (result == kGeneralError) {
      cv.wait(lock);
    }
  }
  ASSERT_EQ(kRemoteOpResponseUninitialised, result);
}

TEST_F(VaultServiceLogicTest, FUNC_MAID_VSL_RemoteVaultAbleToStore) {
  // Setup
  boost::shared_ptr<MockVaultRpcs> mock_rpcs(new MockVaultRpcs(NULL, NULL));
  MockVsl vsl(mock_rpcs, boost::shared_ptr<kad::KNode>());
  vsl.pmid_ = pmid_;
  vsl.pmid_public_signature_ = pmid_public_signature_;
  vsl.pmid_private_ = pmid_private_;
  vsl.online_ = true;
  vsl.our_details_ = our_contact_;
  maidsafe::StoreContract store_contract;

  std::vector<maidsafe::AccountStatusResponse> good_responses;
  std::vector<maidsafe::AccountStatusResponse> good_responses_less_one;
  for (size_t i = 0; i < good_contacts_.size(); ++i) {
    maidsafe::AccountStatusResponse acc_status_response;
    acc_status_response.set_result(kAck);
    acc_status_response.set_pmid(good_contacts_.at(i).node_id());
    good_responses.push_back(acc_status_response);
    if (i < good_contacts_.size() - 1)
      good_responses_less_one.push_back(acc_status_response);
  }
  std::vector<maidsafe::AccountStatusResponse>
      bad_pmid_responses(good_responses);
  std::vector<maidsafe::AccountStatusResponse>
      too_few_ack_responses(good_responses);
  std::vector<maidsafe::AccountStatusResponse>
      fail_initialise_responses(good_responses);
  for (size_t i = kKadLowerThreshold - 1; i < good_contacts_.size(); ++i) {
    bad_pmid_responses.at(i).set_pmid(good_contacts_.at(i - 1).node_id());
    too_few_ack_responses.at(i).set_result(kNack);
    fail_initialise_responses.at(i).clear_result();
  }

  std::string account_owner(crypto_.Hash("Account Owner", "",
      crypto::STRING_STRING, false));
  std::string account_name(crypto_.Hash(account_owner + kAccount, "",
      crypto::STRING_STRING, false));
  maidsafe::AccountStatusRequest request;
  request.set_account_pmid(account_owner);

  boost::mutex mutex;
  boost::condition_variable cv;
  int result(kGeneralError);
  VoidFuncOneInt cb =
      boost::bind(&mock_vsl::CopyResult, _1, &mutex, &cv, &result);

  // Expectations
  EXPECT_CALL(*vsl.kadops(), FindCloseNodes(account_name,
      testing::An<const base::callback_func_type&>()))
      .WillOnce(testing::WithArgs<1>(testing::Invoke(
          boost::bind(&mock_kadops::RunCallback, fail_parse_result_, _1))))
      .WillOnce(testing::WithArgs<1>(testing::Invoke(
          boost::bind(&mock_kadops::RunCallback, fail_result_, _1))))  // Call 2
      .WillOnce(testing::WithArgs<1>(testing::Invoke(
          boost::bind(&mock_kadops::RunCallback, few_result_, _1))))  // Call 3
      .WillOnce(testing::WithArgs<1>(testing::Invoke(
          boost::bind(&mock_kadops::RunCallback, good_result_, _1))))  // Call 4
      .WillOnce(testing::WithArgs<1>(testing::Invoke(
          boost::bind(&mock_kadops::RunCallback, good_result_less_one_, _1))))
      .WillOnce(testing::WithArgs<1>(testing::Invoke(
          boost::bind(&mock_kadops::RunCallback, good_result_, _1))))  // Call 6
      .WillOnce(testing::WithArgs<1>(testing::Invoke(
          boost::bind(&mock_kadops::RunCallback, good_result_, _1))))  // Call 7
      .WillOnce(testing::WithArgs<1>(testing::Invoke(
          boost::bind(&mock_kadops::RunCallback, good_result_, _1))));  // 8

  for (size_t i = 0; i < good_contacts_.size(); ++i) {
    if (i < good_contacts_.size() - 1) {
      EXPECT_CALL(*vsl.kadops(), AddressIsLocal(good_contacts_.at(i)))
          .WillOnce(testing::Return(true))  // Call 4
          .WillOnce(testing::Return(false))  // Call 5
          .WillOnce(testing::Return(true))  // Call 6
          .WillOnce(testing::Return(true))  // Call 7
          .WillOnce(testing::Return(true));  // Call 8
    } else {
      EXPECT_CALL(*vsl.kadops(), AddressIsLocal(good_contacts_.at(i)))
          .WillOnce(testing::Return(true))  // Call 4
          .WillOnce(testing::Return(true))  // Call 6
          .WillOnce(testing::Return(true))  // Call 7
          .WillOnce(testing::Return(true));  // Call 8
    }
  }

  for (size_t i = 0; i < good_contacts_.size(); ++i) {
      EXPECT_CALL(*mock_rpcs, AccountStatus(good_contacts_.at(i), true, 0,
          testing::_, testing::_, testing::_, testing::_))
              .WillOnce(DoAll(testing::SetArgumentPointee<4>(
                                    good_responses.at(i)),  // Call 4
                              testing::WithArgs<6>(testing::Invoke(
                  boost::bind(&mock_vsl::ThreadedDoneRun, 100, 5000, _1)))))
              .WillOnce(DoAll(testing::SetArgumentPointee<4>(
                                    bad_pmid_responses.at(i)),  // Call 6
                              testing::WithArgs<6>(testing::Invoke(
                  boost::bind(&mock_vsl::ThreadedDoneRun, 100, 5000, _1)))))
              .WillOnce(DoAll(testing::SetArgumentPointee<4>(
                                    too_few_ack_responses.at(i)),  // Call 7
                              testing::WithArgs<6>(testing::Invoke(
                  boost::bind(&mock_vsl::ThreadedDoneRun, 100, 5000, _1)))))
              .WillOnce(DoAll(testing::SetArgumentPointee<4>(
                                    fail_initialise_responses.at(i)),  // Call 8
                              testing::WithArgs<6>(testing::Invoke(
                  boost::bind(&mock_vsl::ThreadedDoneRun, 100, 5000, _1)))));
  }
  for (size_t i = 0; i < good_contacts_.size() - 1; ++i) {
      EXPECT_CALL(*mock_rpcs, AccountStatus(good_contacts_.at(i), false, 0,
          testing::_, testing::_, testing::_, testing::_))  // Call 5
              .WillOnce(DoAll(testing::SetArgumentPointee<4>(
                                    good_responses_less_one.at(i)),
                              testing::WithArgs<6>(testing::Invoke(
                  boost::bind(&mock_vsl::ThreadedDoneRun, 100, 5000, _1)))));
  }

  // Call 1 - FindKNodes fails (NULL pointer)
  vsl.RemoteVaultAbleToStore(request, kSuccess, cb, 0);
  {
    boost::mutex::scoped_lock lock(mutex);
    while (result == kGeneralError) {
      cv.wait(lock);
    }
  }
  ASSERT_EQ(kVaultServiceFindNodesError, result);

  // Call 2 - FindKNodes returns kNack
  result = kGeneralError;
  vsl.RemoteVaultAbleToStore(request, kSuccess, cb, 0);
  {
    boost::mutex::scoped_lock lock(mutex);
    while (result == kGeneralError) {
      cv.wait(lock);
    }
  }
  ASSERT_EQ(kVaultServiceFindNodesFailure, result);

  // Call 3 - FindKnodes only returns 1 node
  result = kGeneralError;
  vsl.RemoteVaultAbleToStore(request, kSuccess, cb, 0);
  {
    boost::mutex::scoped_lock lock(mutex);
    while (result == kGeneralError) {
      cv.wait(lock);
    }
  }
  ASSERT_EQ(kVaultServiceFindNodesTooFew, result);

  // Call 4 - All OK
  result = kGeneralError;
  vsl.RemoteVaultAbleToStore(request, kSuccess, cb, 0);
  {
    boost::mutex::scoped_lock lock(mutex);
    while (result == kGeneralError) {
      cv.wait(lock);
    }
  }
  ASSERT_EQ(kSuccess, result);

  // Call 5 - All OK - we're close to chunkname, so we replace contact 16
  result = kGeneralError;
  vsl.RemoteVaultAbleToStore(request, kSuccess, cb, 0);
  {
    boost::mutex::scoped_lock lock(mutex);
    while (result == kGeneralError) {
      cv.wait(lock);
    }
  }
  ASSERT_EQ(kSuccess, result);

  // Call 6 - Fourteen responses have incorrect PMID
  result = kGeneralError;
  vsl.RemoteVaultAbleToStore(request, kSuccess, cb, 0);
  {
    boost::mutex::scoped_lock lock(mutex);
    while (result == kGeneralError) {
      cv.wait(lock);
    }
  }
  ASSERT_EQ(kRemoteOpResponseError, result);

  // Call 7 - Fourteen responses return kNack
  result = kGeneralError;
  vsl.RemoteVaultAbleToStore(request, kSuccess, cb, 0);
  {
    boost::mutex::scoped_lock lock(mutex);
    while (result == kGeneralError) {
      cv.wait(lock);
    }
  }
  ASSERT_EQ(kRemoteOpResponseFailed, result);

  // Call 8 - Fourteen responses don't have result set
  result = kGeneralError;
  vsl.RemoteVaultAbleToStore(request, kSuccess, cb, 0);
  {
    boost::mutex::scoped_lock lock(mutex);
    while (result == kGeneralError) {
      cv.wait(lock);
    }
  }
  ASSERT_EQ(kRemoteOpResponseUninitialised, result);
}

}  // namespace maidsafe_vault
