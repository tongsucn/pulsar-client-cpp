/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
#include <gtest/gtest.h>
#include <pulsar/Client.h>

#include "PulsarFriend.h"
#include "SharedBuffer.h"

using namespace pulsar;

static std::string lookupUrl = "pulsar://localhost:6650";
static const std::string exampleSchema =
    R"({"type":"record","name":"Example","namespace":"test","fields":[{"name":"a","type":"int"},{"name":"b","type":"int"}]})";

TEST(SchemaTest, testSchema) {
    ClientConfiguration config;
    Client client(lookupUrl);
    Result res;

    Producer producer;
    ProducerConfiguration producerConf;
    producerConf.setSchema(SchemaInfo(AVRO, "Avro", exampleSchema));
    res = client.createProducer("topic-avro", producerConf, producer);
    ASSERT_EQ(res, ResultOk);

    // Check schema version
    ASSERT_FALSE(producer.getSchemaVersion().empty());
    producer.close();

    ASSERT_EQ(ResultOk, res);

    // Creating producer with no schema on same topic should fail
    producerConf.setSchema(SchemaInfo(JSON, "Json", "{}"));
    res = client.createProducer("topic-avro", producerConf, producer);
    ASSERT_EQ(ResultIncompatibleSchema, res);

    // Creating producer with no schema on same topic should failed.
    // Because we set broker config isSchemaValidationEnforced=true
    res = client.createProducer("topic-avro", producer);
    ASSERT_EQ(ResultIncompatibleSchema, res);

    ConsumerConfiguration consumerConf;
    Consumer consumer;
    // Subscribing with no schema will still succeed
    res = client.subscribe("topic-avro", "sub-1", consumerConf, consumer);
    ASSERT_EQ(ResultOk, res);

    // Subscribing with same Avro schema will succeed
    consumerConf.setSchema(SchemaInfo(AVRO, "Avro", exampleSchema));
    res = client.subscribe("topic-avro", "sub-2", consumerConf, consumer);
    ASSERT_EQ(ResultOk, res);

    // Subscribing with different schema type will fail
    consumerConf.setSchema(SchemaInfo(JSON, "Json", "{}"));
    res = client.subscribe("topic-avro", "sub-2", consumerConf, consumer);
    ASSERT_EQ(ResultIncompatibleSchema, res);

    client.close();
}

TEST(SchemaTest, testHasSchemaVersion) {
    Client client(lookupUrl);
    std::string topic = "SchemaTest-HasSchemaVersion";
    SchemaInfo stringSchema(SchemaType::STRING, "String", "");

    Consumer consumer;
    ASSERT_EQ(ResultOk, client.subscribe(topic + "1", "sub", ConsumerConfiguration().setSchema(stringSchema),
                                         consumer));
    Producer batchedProducer;
    ASSERT_EQ(ResultOk, client.createProducer(topic + "1", ProducerConfiguration().setSchema(stringSchema),
                                              batchedProducer));
    Producer nonBatchedProducer;
    ASSERT_EQ(ResultOk, client.createProducer(topic + "1", ProducerConfiguration().setSchema(stringSchema),
                                              nonBatchedProducer));

    ASSERT_EQ(ResultOk, batchedProducer.send(MessageBuilder().setContent("msg-0").build()));
    ASSERT_EQ(ResultOk, nonBatchedProducer.send(MessageBuilder().setContent("msg-1").build()));

    Message msgs[2];
    ASSERT_EQ(ResultOk, consumer.receive(msgs[0], 3000));
    ASSERT_EQ(ResultOk, consumer.receive(msgs[1], 3000));

    std::string schemaVersion(8, '\0');
    ASSERT_EQ(msgs[0].getDataAsString(), "msg-0");
    ASSERT_TRUE(msgs[0].hasSchemaVersion());
    ASSERT_EQ(msgs[0].getSchemaVersion(), schemaVersion);

    ASSERT_EQ(msgs[1].getDataAsString(), "msg-1");
    ASSERT_TRUE(msgs[1].hasSchemaVersion());
    ASSERT_EQ(msgs[1].getSchemaVersion(), schemaVersion);

    client.close();
}

TEST(SchemaTest, testKeyValueSchema) {
    SchemaInfo keySchema(SchemaType::AVRO, "String", exampleSchema);
    SchemaInfo valueSchema(SchemaType::AVRO, "String", exampleSchema);
    SchemaInfo keyValueSchema(keySchema, valueSchema, KeyValueEncodingType::INLINE);
    ASSERT_EQ(keyValueSchema.getSchemaType(), KEY_VALUE);
    ASSERT_EQ(keyValueSchema.getSchema().size(),
              8 + keySchema.getSchema().size() + valueSchema.getSchema().size());
}

TEST(SchemaTest, testKeySchemaIsEmpty) {
    SchemaInfo keySchema(SchemaType::AVRO, "String", "");
    SchemaInfo valueSchema(SchemaType::AVRO, "String", exampleSchema);
    SchemaInfo keyValueSchema(keySchema, valueSchema, KeyValueEncodingType::INLINE);
    ASSERT_EQ(keyValueSchema.getSchemaType(), KEY_VALUE);
    ASSERT_EQ(keyValueSchema.getSchema().size(),
              8 + keySchema.getSchema().size() + valueSchema.getSchema().size());

    SharedBuffer buffer = SharedBuffer::wrap(const_cast<char*>(keyValueSchema.getSchema().c_str()),
                                             keyValueSchema.getSchema().size());
    int keySchemaSize = buffer.readUnsignedInt();
    ASSERT_EQ(keySchemaSize, -1);
    int valueSchemaSize = buffer.readUnsignedInt();
    ASSERT_EQ(valueSchemaSize, valueSchema.getSchema().size());
    std::string valueSchemaStr(buffer.slice(0, valueSchemaSize).data(), valueSchemaSize);
    ASSERT_EQ(valueSchema.getSchema(), valueSchemaStr);
}

TEST(SchemaTest, testValueSchemaIsEmpty) {
    SchemaInfo keySchema(SchemaType::AVRO, "String", exampleSchema);
    SchemaInfo valueSchema(SchemaType::AVRO, "String", "");
    SchemaInfo keyValueSchema(keySchema, valueSchema, KeyValueEncodingType::INLINE);
    ASSERT_EQ(keyValueSchema.getSchemaType(), KEY_VALUE);
    ASSERT_EQ(keyValueSchema.getSchema().size(),
              8 + keySchema.getSchema().size() + valueSchema.getSchema().size());

    SharedBuffer buffer = SharedBuffer::wrap(const_cast<char*>(keyValueSchema.getSchema().c_str()),
                                             keyValueSchema.getSchema().size());
    int keySchemaSize = buffer.readUnsignedInt();
    ASSERT_EQ(keySchemaSize, keySchema.getSchema().size());
    std::string keySchemaStr(buffer.slice(0, keySchemaSize).data(), keySchemaSize);
    ASSERT_EQ(keySchemaStr, keySchema.getSchema());
    buffer.consume(keySchemaSize);
    int valueSchemaSize = buffer.readUnsignedInt();
    ASSERT_EQ(valueSchemaSize, -1);
}

TEST(SchemaTest, testAutoDownloadSchema) {
    const std::string topic = "testAutoPublicSchema" + std::to_string(time(nullptr));
    std::string jsonSchema =
        R"({"type":"record","name":"cpx","fields":[{"name":"re","type":"double"},{"name":"im","type":"double"}]})";
    SchemaInfo schema(JSON, "test-schema", jsonSchema);

    Client client(lookupUrl);

    ConsumerConfiguration consumerConfiguration;
    consumerConfiguration.setSchema(schema);
    Consumer consumer;
    ASSERT_EQ(ResultOk, client.subscribe(topic, "t-sub", consumerConfiguration, consumer));

    ProducerConfiguration producerConfiguration;
    Producer producer;

    auto clientImplPtr = PulsarFriend::getClientImplPtr(client);

    Promise<Result, Producer> promise;
    clientImplPtr->createProducerAsync(topic, producerConfiguration, WaitForCallbackValue<Producer>(promise),
                                       true);
    ASSERT_EQ(ResultOk, promise.getFuture().get(producer));

    Message msg = MessageBuilder().setContent("content").build();
    ASSERT_EQ(ResultOk, producer.send(msg));

    ASSERT_EQ(ResultOk, consumer.receive(msg));
    ASSERT_EQ("content", msg.getDataAsString());
}
