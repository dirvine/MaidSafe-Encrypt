﻿/*******************************************************************************
 *  Copyright 2008-2011 maidsafe.net limited                                   *
 *                                                                             *
 *  The following source code is property of maidsafe.net limited and is not   *
 *  meant for external use.  The use of this code is governed by the license   *
 *  file LICENSE.TXT found in the root of this directory and also on           *
 *  www.maidsafe.net.                                                          *
 *                                                                             *
 *  You are not free to copy, amend or otherwise use this source code without  *
 *  the explicit written permission of the board of directors of maidsafe.net. *
 ***************************************************************************//**
 * @file  utils.cc
 * @brief Helper functions for self-encryption engine.
 * @date  2008-09-09
 */

#include "maidsafe/encrypt/self_encrypt.h"

#include <algorithm>
#include <set>
#include <tuple>
#ifdef __MSVC__
#  pragma warning(push, 1)
#  pragma warning(disable: 4702)
#endif

#include <omp.h>
#include "cryptopp/gzip.h"
#include "cryptopp/hex.h"
#include "cryptopp/aes.h"
#include "cryptopp/modes.h"
#include "cryptopp/rsa.h"
#include "cryptopp/osrng.h"
#include "cryptopp/integer.h"
#include "cryptopp/pwdbased.h"
#include "cryptopp/cryptlib.h"
#include "cryptopp/filters.h"
#include "cryptopp/channels.h"
#include "cryptopp/mqueue.h"

#ifdef __MSVC__
#  pragma warning(pop)
#endif
#include "boost/shared_array.hpp"
#include "boost/thread.hpp"
#include "boost/filesystem/fstream.hpp"
#include "boost/scoped_array.hpp"
#include "maidsafe/common/crypto.h"
#include "maidsafe/common/utils.h"
#include "maidsafe/common/chunk_store.h"
#include "maidsafe/encrypt/config.h"
#include "maidsafe/encrypt/data_map.h"
#include "maidsafe/encrypt/log.h"

namespace fs = boost::filesystem;

namespace maidsafe {

namespace encrypt {
/**
 * Implementation of XOR transformation filter to allow pipe-lining
 *
 */
size_t XORFilter::Put2(const byte* inString,
                      size_t length,
                      int messageEnd,
                      bool blocking) {
  if ((length == 0))
    return AttachedTransformation()->Put2(inString,
                                          length,
                                          messageEnd,
                                          blocking);
  boost::scoped_array<byte> buffer(new byte[length]);
// #pragme omp parallel for 
  for (size_t i = 0; i < length; ++i) {
    buffer[i] = inString[i] ^  pad_[count_%144];
    ++count_;
  }
  return AttachedTransformation()->Put2(buffer.get(),
                                       length,
                                        messageEnd,
                                        blocking);
}

bool SE::Write(const char* data, size_t length, size_t position) {

  if (length == 0)
    return true;
  // FIXME FIXME check sequencer
    
  if (position == current_position_) {
    main_encrypt_queue_.Put2(const_cast<byte*>
                           (reinterpret_cast<const byte*>(data)),
                            length, -1, true);
    // check sequencer for more data
    sequence_data extra(sequencer_.Get(current_position_+length));
    if (extra.second != 0) {
    main_encrypt_queue_.Put2(const_cast<byte*>
                           (reinterpret_cast<const byte*>(extra.first)),
                            extra.second, -1, true);
    current_position_ += extra.second;
    }
    current_position_ += length;
    
    
  } else if (position < current_position_) {
    // TODO (dirvine) handle rewrites properly
    // need to grab data and rewrite it
    // check sequencer
  } else {
    //std::string add_this(data, length);
    
    sequencer_.Add(position, const_cast<char *>(data), length);
  }
    // Do not queue chunks 0 and 1 till we know we have enough for 3 chunks
    if (main_encrypt_queue_.MaxRetrievable() >= chunk_size_ * 3) {
      if (!chunk_one_two_q_full_)
        QueueC1AndC2();
       return ProcessMainQueue();
    } else
      return true;  // not enough to process chunks yet
}


bool SE::FinaliseWrite() {
  // FIXME process sequencer 
  chunk_size_ = (main_encrypt_queue_.TotalBytesRetrievable()) / 3 ;
  if ((chunk_size_) < 1025) {
    return ProcessLastData();
  }
  while (main_encrypt_queue_.TotalBytesRetrievable() < chunk_size_ * 3) {
    // small files direct to data map
    if ((chunk_size_) < 1025) {
       return ProcessLastData();
    }
    ProcessMainQueue();
  }
  #pragma omp parallel 
  if (chunk_one_two_q_full_) {
    EncryptAChunk(0, chunk0_raw_.get(), c0_and_1_chunk_size_, false);
    EncryptAChunk(1, chunk1_raw_.get(), c0_and_1_chunk_size_, false);
    chunk_one_two_q_full_ = false;
  }
  return ProcessLastData();
}

bool SE::DeleteAllChunks()
{
  for (size_t i =0; i < data_map_->chunks.size(); ++i)
    if (!chunk_store_->Delete(reinterpret_cast<char *>
                      (data_map_->chunks[i].hash)))
      return false;
  return true;
}

bool SE::DeleteAChunk(size_t chunk_num)
{
  if (!chunk_store_->Delete(reinterpret_cast<char *>
    (data_map_->chunks[chunk_num].hash)))
    return false;
  return true;
}


bool SE::ProcessLastData() {
  size_t qlength = main_encrypt_queue_.TotalBytesRetrievable();
    boost::scoped_array<byte> i(new byte[qlength]);
    main_encrypt_queue_.Get(i.get(), sizeof(i));
    data_map_->content = reinterpret_cast<const char *>(i.get());
    data_map_->content_size = qlength;
    data_map_->size += qlength;
    if (chunk_one_two_q_full_) {
      EncryptAChunk(0, chunk0_raw_.get(), c0_and_1_chunk_size_, false);
      EncryptAChunk(1, chunk1_raw_.get(), c0_and_1_chunk_size_, false);
      chunk_one_two_q_full_ = false;
    }
    main_encrypt_queue_.SkipAll();
    return true;
}

bool SE::ReInitialise() {
    chunk_size_ = 1024*256;
    main_encrypt_queue_.SkipAll();
    chunk0_queue_.SkipAll();
    chunk1_queue_.SkipAll();
    chunk_one_two_q_full_ = false;
    data_map_.reset(new DataMap2);
    return true;
}

bool SE::QueueC1AndC2() {
  c0_and_1_chunk_size_ = chunk_size_;
  // Chunk 1
  main_encrypt_queue_.Get(chunk0_raw_.get(), chunk_size_);
  ChunkDetails2 chunk_data;
  CryptoPP::SHA512().CalculateDigest(chunk_data.pre_hash,
                                     chunk0_raw_.get(),
                                     chunk_size_);
  chunk_data.pre_size = chunk_size_;
  data_map_->chunks.push_back(chunk_data);
  
  // Chunk 2
  main_encrypt_queue_.Get(chunk1_raw_.get(), chunk_size_);
  ChunkDetails2 chunk_data2;
  CryptoPP::SHA512().CalculateDigest(chunk_data2.pre_hash,
                                     chunk1_raw_.get() ,
                                     chunk_size_);
  chunk_data2.pre_size = chunk_size_;
  data_map_->chunks.push_back(chunk_data);
  chunk_one_two_q_full_ = true;
  return chunk_one_two_q_full_;
}

bool SE::ProcessMainQueue() {
  if (!main_encrypt_queue_.MaxRetrievable()  >= chunk_size_)
    return false;

  size_t chunks_to_process = (main_encrypt_queue_.MaxRetrievable() / chunk_size_);
  size_t old_dm_size = data_map_->chunks.size();
  data_map_->chunks.resize(chunks_to_process + old_dm_size);
  std::vector<boost::shared_array<byte>>chunk_vec(chunks_to_process,
                                               boost::shared_array<byte
                                               >(new byte[chunk_size_]));
  //get all hashes
   for(size_t i = 0; i < chunks_to_process; ++i) {
     boost::shared_array<byte> tempy(new byte[chunk_size_]);
     main_encrypt_queue_.Get(tempy.get(), chunk_size_);
     chunk_vec[i] = tempy;
   }
#pragma omp parallel for
   for(size_t i = 0; i < chunks_to_process; ++i) {
     CryptoPP::SHA512().CalculateDigest(data_map_->chunks[i + old_dm_size].pre_hash,
           chunk_vec[i].get(),
           chunk_size_);
    data_map_->chunks[i + old_dm_size].pre_size = chunk_size_;
    }
  // process chunks
#pragma omp parallel for // gives over 100Mb write speeds
  for(size_t j = 0; j < chunks_to_process; ++j) {

    EncryptAChunk(j + old_dm_size,
                  &chunk_vec[j][0],
                  chunk_size_,
                  false);
  }
  return true;
}

void SE::HashMe(byte * digest, byte* data, size_t length) {
  CryptoPP::SHA512().CalculateDigest(digest, data, length);
}

void SE::getPad_Iv_Key(size_t this_chunk_num,
                       boost::shared_array<byte> key,
                       boost::shared_array<byte> iv,
                       boost::shared_array<byte> pad) {
  size_t num_chunks = data_map_->chunks.size();
  size_t n_1_chunk = (this_chunk_num + num_chunks -1) % num_chunks;
  size_t n_2_chunk = (this_chunk_num + num_chunks -2) % num_chunks;
#pragma omp parallel for shared(key, iv)
  for (int i = 0; i < 48; ++i) {
    if (i < 32)
      key[i] = data_map_->chunks[n_1_chunk].pre_hash[i];
    if (i > 31)
      iv[i - 32] = data_map_->chunks[n_1_chunk].pre_hash[i];
  }
#pragma omp parallel for shared(pad)
  for (int i = 0; i < 64; ++i) {
    pad[i] =  data_map_->chunks[n_1_chunk].pre_hash[i];
    pad[i+64] = data_map_->chunks[this_chunk_num].pre_hash[i];
    if (i < 16)
      pad[i+128] = data_map_->chunks[n_2_chunk].pre_hash[i+48];
  }
}


bool SE::EncryptAChunk(size_t chunk_num, byte* data,
                       size_t length, bool re_encrypt) {
  if (data_map_->chunks.size() < chunk_num)
    return false;
   if (re_encrypt)  // fix pre enc hash and re-encrypt next 2
     CryptoPP::SHA512().CalculateDigest(data_map_->chunks[chunk_num].pre_hash,
                                        data,
                                        length);

  boost::shared_array<byte> pad(new byte[144]);
  boost::shared_array<byte> key(new byte[32]);
  boost::shared_array<byte> iv (new byte[16]);
  getPad_Iv_Key(chunk_num, key, iv, pad);

  CryptoPP::CFB_Mode<CryptoPP::AES>::Encryption encryptor(key.get(),
                                                          32,
                                                          iv.get());
  CryptoPP::StreamTransformationFilter aes_filter(encryptor,
                  new XORFilter(
                    new CryptoPP::HashFilter(hash_,
                      new CryptoPP::MessageQueue()
                    , true)
                  , pad.get()));
  
  aes_filter.Put2(data, length, -1, true);

  boost::scoped_array<byte> chunk_content(new byte [length]);
  aes_filter.Get(chunk_content.get(), length); // get content
  aes_filter.Get(data_map_->chunks[chunk_num].hash , 64);
  std::string post_hash(reinterpret_cast<char *>
                          (data_map_->chunks[chunk_num].hash), 64);
  std::string data_to_store(reinterpret_cast<const char *>(chunk_content.get()),
                            length);

#pragma omp critical
{
  if (! chunk_store_->Store(post_hash, data_to_store)) 
    DLOG(ERROR) << "Could not store " << EncodeToHex(post_hash)
                                        << std::endl;
}
   if (!re_encrypt) {
    data_map_->chunks[chunk_num].size = length;
    data_map_->size += length;
   }
  return true;
}


bool SE::Read(char* data, size_t length, size_t position) {

   if ((data_map_->size > (length + position)) && (length != 0))
     return false;

   size_t start_chunk(0), start_offset(0), end_chunk(0), run_total(0);
// #pragma omp parallel for shared(start_chunk, start_offset, end_chunk, run_total)
   for(size_t i = 0; i < data_map_->chunks.size(); ++i) {
     if ((data_map_->chunks[i].size + run_total >= position) &&
         (start_chunk = 0)) {
       start_chunk = i;
       start_offset = run_total + data_map_->chunks[i].size -
                      (position - run_total);
       run_total = data_map_->chunks[i].size - start_offset;
     }
     else
#pragma omp atomic
       run_total += data_map_->chunks[i].size;
           // find end (offset handled by return truncated size
     if ((run_total <= length) || (length == 0))
       end_chunk = i;
   }

   if (end_chunk != 0)
     ++end_chunk;

#pragma omp parallel for shared(data)
  for (size_t i = start_chunk;i < end_chunk ; ++i) {
    size_t this_chunk_size(0);
    for (size_t j = start_chunk; j < i; ++j)
#pragma omp atomic
      this_chunk_size += data_map_->chunks[j].size;
    ReadChunk(i, reinterpret_cast<byte *>(&data[this_chunk_size])); 
  }
 
  for(size_t i = 0; i < data_map_->content_size; ++i) {
#pragma omp barrier
    data[length - data_map_->content_size + i] = data_map_->content[i];
  }
  return readok_;
}

void SE::ReadChunk(size_t chunk_num, byte *data) {
  if (data_map_->chunks.size() < chunk_num) {
    readok_ = false;
    return;
  }
   std::string hash(reinterpret_cast<char *>(data_map_->chunks[chunk_num].hash),
                    64);
  size_t length = data_map_->chunks[chunk_num].size;
  boost::shared_array<byte> pad(new byte[144]);
  boost::shared_array<byte> key(new byte[32]);
  boost::shared_array<byte> iv (new byte[16]);
  getPad_Iv_Key(chunk_num, key, iv, pad);
  std::string content("");
#pragma omp critical
{
  content = chunk_store_->Get(hash);
}
  if (content == ""){
    DLOG(ERROR) << "Could not find chunk: " << EncodeToHex(hash) << std::endl;
    readok_ = false;
    return;
  }
    
  CryptoPP::CFB_Mode<CryptoPP::AES>::Decryption decryptor(key.get(), 32, iv.get());
          CryptoPP::StringSource filter(content, true,
            new XORFilter(
            new CryptoPP::StreamTransformationFilter(decryptor,
              new CryptoPP::MessageQueue),
            pad.get()));
  filter.Get(data, length);
}

}  // namespace encrypt

}  // namespace maidsafe