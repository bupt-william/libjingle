/*
 * libjingle
 * Copyright 2012 Google Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <string>

#include "talk/base/buffer.h"
#include "talk/base/gunit.h"
#include "talk/base/helpers.h"
#include "talk/base/scoped_ptr.h"
#include "talk/base/timing.h"
#include "talk/session/phone/constants.h"
#include "talk/session/phone/dataengine.h"
#include "talk/session/phone/fakenetworkinterface.h"
#include "talk/session/phone/rtputils.h"

class FakeTiming : public talk_base::Timing {
 public:
  FakeTiming() : now_(0.0) {}

  virtual double TimerNow() {
    return now_;
  }

  void set_now(double now) {
    now_ = now;
  }

 private:
  double now_;
};

class FakeDataReceiver : public cricket::DataMediaChannel::Receiver {
 public:
  FakeDataReceiver() : has_received_data_(false) {}

  virtual void ReceiveData(
      const cricket::DataMediaChannel::ReceiveDataParams& params,
      const char* data, size_t len) {
    has_received_data_ = true;
    last_received_data_ = std::string(data, len);
    last_received_data_len_ = len;
    last_received_data_params_ = params;
  }

  bool has_received_data() const { return has_received_data_; }
  std::string last_received_data() const { return last_received_data_; }
  size_t last_received_data_len() const { return last_received_data_len_; }
  cricket::DataMediaChannel::ReceiveDataParams
      last_received_data_params() const {
    return last_received_data_params_;
  }

 private:
  bool has_received_data_;
  std::string last_received_data_;
  size_t last_received_data_len_;
  cricket::DataMediaChannel::ReceiveDataParams last_received_data_params_;
};

class DataMediaChannelTest : public testing::Test {
 protected:
  virtual void SetUp() {
    // Seed needed for each test to satisfy expectations.
    iface_.reset(new cricket::FakeNetworkInterface());
    timing_ = new FakeTiming();
    dme_.reset(CreateEngine(timing_));
    receiver_.reset(new FakeDataReceiver());
  }

  void SetNow(double now) {
    timing_->set_now(now);
  }

  cricket::DataEngine* CreateEngine(FakeTiming* timing) {
    cricket::DataEngine* dme = new cricket::DataEngine();
    dme->SetTiming(timing);
    return dme;
  }

  cricket::DataMediaChannel* CreateChannel() {
    return CreateChannel(dme_.get());
  }

  cricket::DataMediaChannel* CreateChannel(cricket::DataEngine* dme) {
    cricket::DataMediaChannel* channel = dme->CreateChannel();
    channel->SetInterface(iface_.get());
    return channel;
  }

  cricket::DataMediaChannel::Receiver* receiver() {
    return receiver_.get();
  }

  bool HasReceivedData() {
    return receiver_->has_received_data();
  }

  std::string GetReceivedData() {
    return receiver_->last_received_data();
  }

  size_t GetReceivedDataLen() {
    return receiver_->last_received_data_len();
  }

  cricket::DataMediaChannel::ReceiveDataParams GetReceivedDataParams() {
    return receiver_->last_received_data_params();
  }

  bool HasSentData(int count) {
    return (iface_->NumRtpPackets() > count);
  }

  std::string GetSentData(int index) {
    // Assume RTP header of length 12
    const talk_base::Buffer* packet = iface_->GetRtpPacket(index);
    if (packet->length() > 12) {
      return std::string(packet->data() + 12, packet->length() - 12);
    } else {
      return "";
    }
  }

  cricket::RtpHeader GetSentDataHeader(int index) {
    const talk_base::Buffer* packet = iface_->GetRtpPacket(index);
    cricket::RtpHeader header;
    GetRtpHeader(packet->data(), packet->length(), &header);
    return header;
  }

 private:
  talk_base::scoped_ptr<cricket::DataEngine> dme_;
  // Timing passed into dme_.  Owned by dme_;
  FakeTiming* timing_;
  talk_base::scoped_ptr<cricket::FakeNetworkInterface> iface_;
  talk_base::scoped_ptr<FakeDataReceiver> receiver_;
};

TEST_F(DataMediaChannelTest, SetUnknownCodecs) {
  talk_base::scoped_ptr<cricket::DataMediaChannel> dmc(CreateChannel());

  cricket::DataCodec known_codec;
  known_codec.id = 103;
  known_codec.name = "google-data";
  cricket::DataCodec unknown_codec;
  unknown_codec.id = 104;
  unknown_codec.name = "unknown-data";

  std::vector<cricket::DataCodec> known_codecs;
  known_codecs.push_back(known_codec);

  std::vector<cricket::DataCodec> unknown_codecs;
  unknown_codecs.push_back(unknown_codec);

  std::vector<cricket::DataCodec> mixed_codecs;
  mixed_codecs.push_back(known_codec);
  mixed_codecs.push_back(unknown_codec);

  EXPECT_TRUE(dmc->SetSendCodecs(known_codecs));
  EXPECT_FALSE(dmc->SetSendCodecs(unknown_codecs));
  EXPECT_TRUE(dmc->SetSendCodecs(mixed_codecs));
  EXPECT_TRUE(dmc->SetRecvCodecs(known_codecs));
  EXPECT_FALSE(dmc->SetRecvCodecs(unknown_codecs));
  EXPECT_FALSE(dmc->SetRecvCodecs(mixed_codecs));
}

TEST_F(DataMediaChannelTest, SendData) {
  talk_base::scoped_ptr<cricket::DataMediaChannel> dmc(CreateChannel());

  cricket::DataMediaChannel::SendDataParams params;
  params.ssrc = 42;
  std::string data = "food";
  unsigned char padded_data[] = {
    0x00, 0x00, 0x00, 0x00,
    'f', 'o', 'o', 'd',
  };

  // NULL data
  EXPECT_FALSE(dmc->SendData(params, NULL, data.length()));
  EXPECT_FALSE(HasSentData(0));

  // Negative length
  EXPECT_FALSE(dmc->SendData(params, data.data(), -1));
  EXPECT_FALSE(HasSentData(0));

  // Not sending
  EXPECT_FALSE(dmc->SendData(params, data.data(), data.length()));
  EXPECT_FALSE(HasSentData(0));
  ASSERT_TRUE(dmc->SetSend(true));

  // Unknown stream name.
  EXPECT_FALSE(dmc->SendData(params, data.data(), data.length()));
  EXPECT_FALSE(HasSentData(0));

  cricket::StreamParams stream;
  stream.add_ssrc(42);
  ASSERT_TRUE(dmc->AddSendStream(stream));

  // Unknown codec;
  EXPECT_FALSE(dmc->SendData(params, data.data(), data.length()));
  EXPECT_FALSE(HasSentData(0));

  cricket::DataCodec codec;
  codec.id = 103;
  codec.name = cricket::kGoogleDataCodecName;
  std::vector<cricket::DataCodec> codecs;
  codecs.push_back(codec);
  ASSERT_TRUE(dmc->SetSendCodecs(codecs));

  // Length too large;
  EXPECT_FALSE(dmc->SendData(params, data.data(), 10000000));
  EXPECT_FALSE(HasSentData(0));

  // Finally works!
  EXPECT_TRUE(dmc->SendData(params, data.data(), data.length()));
  ASSERT_TRUE(HasSentData(0));
  EXPECT_EQ(sizeof(padded_data), GetSentData(0).length());
  EXPECT_EQ(0, memcmp(
      padded_data, GetSentData(0).data(), sizeof(padded_data)));
  cricket::RtpHeader header0 = GetSentDataHeader(0);
  EXPECT_NE(0, header0.seq_num);
  EXPECT_NE(0U, header0.timestamp);
  EXPECT_EQ(header0.ssrc, 42U);
  EXPECT_EQ(header0.payload_type, 103);

  // Should bump timestamp by 180000 because the clock rate is 90khz.
  SetNow(2);

  EXPECT_TRUE(dmc->SendData(params, data.data(), data.length()));
  ASSERT_TRUE(HasSentData(1));
  EXPECT_EQ(sizeof(padded_data), GetSentData(1).length());
  EXPECT_EQ(0, memcmp(
      padded_data, GetSentData(1).data(), sizeof(padded_data)));
  cricket::RtpHeader header1 = GetSentDataHeader(1);
  EXPECT_EQ(header1.ssrc, 42U);
  EXPECT_EQ(header1.payload_type, 103);
  EXPECT_EQ(header0.seq_num + 1, header1.seq_num);
  EXPECT_EQ(header0.timestamp + 180000, header1.timestamp);
}

TEST_F(DataMediaChannelTest, SendDataMultipleClocks) {
  // Timings owned by DataEngines.
  FakeTiming* timing1 = new FakeTiming();
  talk_base::scoped_ptr<cricket::DataEngine> dme1(CreateEngine(timing1));
  talk_base::scoped_ptr<cricket::DataMediaChannel> dmc1(
      CreateChannel(dme1.get()));
  FakeTiming* timing2 = new FakeTiming();
  talk_base::scoped_ptr<cricket::DataEngine> dme2(CreateEngine(timing2));
  talk_base::scoped_ptr<cricket::DataMediaChannel> dmc2(
      CreateChannel(dme2.get()));

  ASSERT_TRUE(dmc1->SetSend(true));
  ASSERT_TRUE(dmc2->SetSend(true));

  cricket::StreamParams stream1;
  stream1.add_ssrc(41);
  ASSERT_TRUE(dmc1->AddSendStream(stream1));
  cricket::StreamParams stream2;
  stream2.add_ssrc(42);
  ASSERT_TRUE(dmc2->AddSendStream(stream2));

  cricket::DataCodec codec;
  codec.id = 103;
  codec.name = cricket::kGoogleDataCodecName;
  std::vector<cricket::DataCodec> codecs;
  codecs.push_back(codec);
  ASSERT_TRUE(dmc1->SetSendCodecs(codecs));
  ASSERT_TRUE(dmc2->SetSendCodecs(codecs));

  cricket::DataMediaChannel::SendDataParams params1;
  params1.ssrc = 41;
  cricket::DataMediaChannel::SendDataParams params2;
  params2.ssrc = 42;

  std::string data = "foo";

  EXPECT_TRUE(dmc1->SendData(params1, data.data(), data.length()));
  EXPECT_TRUE(dmc2->SendData(params2, data.data(), data.length()));

  // Should bump timestamp by 90000 because the clock rate is 90khz.
  timing1->set_now(1);
  // Should bump timestamp by 180000 because the clock rate is 90khz.
  timing2->set_now(2);

  EXPECT_TRUE(dmc1->SendData(params1, data.data(), data.length()));
  EXPECT_TRUE(dmc2->SendData(params2, data.data(), data.length()));

  ASSERT_TRUE(HasSentData(3));
  cricket::RtpHeader header1a = GetSentDataHeader(0);
  cricket::RtpHeader header2a = GetSentDataHeader(1);
  cricket::RtpHeader header1b = GetSentDataHeader(2);
  cricket::RtpHeader header2b = GetSentDataHeader(3);

  EXPECT_EQ(header1a.seq_num + 1, header1b.seq_num);
  EXPECT_EQ(header1a.timestamp + 90000, header1b.timestamp);
  EXPECT_EQ(header2a.seq_num + 1, header2b.seq_num);
  EXPECT_EQ(header2a.timestamp + 180000, header2b.timestamp);
}

TEST_F(DataMediaChannelTest, ReceiveData) {
  // PT= 103, SN=2, TS=3, SSRC = 4, data = "abcde"
  unsigned char data[] = {
    0x80, 0x67, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x2A,
    0x00, 0x00, 0x00, 0x00,
    'a', 'b', 'c', 'd', 'e'
  };
  talk_base::Buffer packet(data, sizeof(data));

  talk_base::scoped_ptr<cricket::DataMediaChannel> dmc(CreateChannel());

  // SetReceived not called.
  dmc->OnPacketReceived(&packet);
  EXPECT_FALSE(HasReceivedData());

  dmc->SetReceive(true);

  // Unknown payload id
  dmc->OnPacketReceived(&packet);
  EXPECT_FALSE(HasReceivedData());

  cricket::DataCodec codec;
  codec.id = 103;
  codec.name = cricket::kGoogleDataCodecName;
  std::vector<cricket::DataCodec> codecs;
  codecs.push_back(codec);
  ASSERT_TRUE(dmc->SetRecvCodecs(codecs));

  // Unknown stream
  dmc->OnPacketReceived(&packet);
  EXPECT_FALSE(HasReceivedData());

  cricket::StreamParams stream;
  stream.add_ssrc(42);
  ASSERT_TRUE(dmc->AddRecvStream(stream));

  // No receiver set
  dmc->OnPacketReceived(&packet);
  EXPECT_FALSE(HasReceivedData());

  dmc->SetReceiver(42, receiver());

  // Finally works!
  dmc->OnPacketReceived(&packet);
  EXPECT_TRUE(HasReceivedData());
  EXPECT_EQ("abcde", GetReceivedData());
  EXPECT_EQ(5U, GetReceivedDataLen());
}

TEST_F(DataMediaChannelTest, InvalidRtpPackets) {
  unsigned char data[] = {
    0x80, 0x65, 0x00, 0x02
  };
  talk_base::Buffer packet(data, sizeof(data));

  talk_base::scoped_ptr<cricket::DataMediaChannel> dmc(CreateChannel());

  // Too short
  dmc->OnPacketReceived(&packet);
  EXPECT_FALSE(HasReceivedData());
}
