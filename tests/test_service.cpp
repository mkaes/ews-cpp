#include "fixtures.hpp"
#include <utility>

using on_scope_exit = ews::internal::on_scope_exit;

namespace tests
{
    class RequestServerVersionTest : public FakeServiceFixture
    {
    private:
        void SetUp()
        {
            FakeServiceFixture::SetUp();
            set_next_fake_response(
R"(
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">
    <s:Header>
        <h:ServerVersionInfo MajorVersion="15" MinorVersion="0" MajorBuildNumber="847" MinorBuildNumber="31" Version="V2_8" xmlns:h="http://schemas.microsoft.com/exchange/services/2006/types" xmlns="http://schemas.microsoft.com/exchange/services/2006/types" xmlns:xsd="http://www.w3.org/2001/XMLSchema" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"/>
    </s:Header>
    <s:Body xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmlns:xsd="http://www.w3.org/2001/XMLSchema">
        <m:CreateItemResponse xmlns:m="http://schemas.microsoft.com/exchange/services/2006/messages" xmlns:t="http://schemas.microsoft.com/exchange/services/2006/types">
            <m:ResponseMessages>
                <m:CreateItemResponseMessage ResponseClass="Success">
                    <m:ResponseCode>NoError</m:ResponseCode>
                    <m:Items>
                        <t:Message>
                            <t:ItemId Id="AAMkAGRhYmQ5Njg0LTNhMjEtNDZkOS1hN2QyLTUzZTI3MjdhN2ZkYgBGAAAAAAC5LuzvcattTqJiAgNAfv18BwDKOL2xzF+1SL9YsnXMX2cZAAAAAAEQAADKOL2xzF+1SL9YsnXMX2cZAAADQIPPAAA=" ChangeKey="CQAAABYAAADKOL2xzF+1SL9YsnXMX2cZAAADQKty"/>
                        </t:Message>
                    </m:Items>
                </m:CreateItemResponseMessage>
            </m:ResponseMessages>
        </m:CreateItemResponse>
    </s:Body>
</s:Envelope>
)"
            );
        }
    };

    TEST_F(RequestServerVersionTest, SupportsExchange2007)
    {
        auto& serv = service();
        serv.set_request_server_version(ews::server_version::exchange_2007);
        auto task = ews::task();
        task.set_subject("Random To-Do item");
        serv.create_item(task);
        auto request = get_last_request();
        EXPECT_TRUE(request.header_contains(
                    R"(<t:RequestServerVersion Version="Exchange2007"/>)"));
    }

    TEST_F(RequestServerVersionTest, SupportsExchange2007_SP1)
    {
        auto& serv = service();
        serv.set_request_server_version(ews::server_version::exchange_2007_sp1);
        auto task = ews::task();
        task.set_subject("Another To-Do item");
        serv.create_item(task);
        auto request = get_last_request();
        EXPECT_TRUE(request.header_contains(
                    R"(<t:RequestServerVersion Version="Exchange2007_SP1"/>)"));
    }

    TEST_F(RequestServerVersionTest, SupportsExchange2010)
    {
        auto& serv = service();
        serv.set_request_server_version(ews::server_version::exchange_2010);
        auto task = ews::task();
        task.set_subject("Yet another To-Do item");
        serv.create_item(task);
        auto request = get_last_request();
        EXPECT_TRUE(request.header_contains(
                    R"(<t:RequestServerVersion Version="Exchange2010"/>)"));
    }

    TEST_F(RequestServerVersionTest, SupportsExchange2010_SP1)
    {
        auto& serv = service();
        serv.set_request_server_version(ews::server_version::exchange_2010_sp1);
        auto task = ews::task();
        task.set_subject("Feed the cat");
        serv.create_item(task);
        auto request = get_last_request();
        EXPECT_TRUE(request.header_contains(
                    R"(<t:RequestServerVersion Version="Exchange2010_SP1"/>)"));
    }

    TEST_F(RequestServerVersionTest, SupportsExchange2010_SP2)
    {
        auto& serv = service();
        serv.set_request_server_version(ews::server_version::exchange_2010_sp2);
        auto task = ews::task();
        task.set_subject("Do something about code duplication");
        serv.create_item(task);
        auto request = get_last_request();
        EXPECT_TRUE(request.header_contains(
                    R"(<t:RequestServerVersion Version="Exchange2010_SP2"/>)"));
    }

    TEST_F(RequestServerVersionTest, SupportsExchange2013)
    {
        auto& serv = service();
        serv.set_request_server_version(ews::server_version::exchange_2013);
        auto task = ews::task();
        task.set_subject("Buy new shoes");
        serv.create_item(task);
        auto request = get_last_request();
        EXPECT_TRUE(request.header_contains(
                    R"(<t:RequestServerVersion Version="Exchange2013"/>)"));
    }

    TEST_F(RequestServerVersionTest, SupportsExchange2013_SP1)
    {
        auto& serv = service();
        serv.set_request_server_version(ews::server_version::exchange_2013_sp1);
        auto task = ews::task();
        task.set_subject("Get some milk from the store");
        serv.create_item(task);
        auto request = get_last_request();
        EXPECT_TRUE(request.header_contains(
                    R"(<t:RequestServerVersion Version="Exchange2013_SP1"/>)"));
    }

    TEST_F(RequestServerVersionTest, DefaultServerVersionIs2013_SP1)
    {
        auto serv = service();
        EXPECT_EQ(serv.get_request_server_version(),
                  ews::server_version::exchange_2013_sp1);
    }

    class ServiceTest : public ContactTest {};

    TEST_F(ServiceTest, UpdateItemOfReadOnlyPropertyThrows)
    {
        ews::item_property_path item_property;
        auto minnie = test_contact();
        ASSERT_FALSE(minnie.has_attachments());
        auto prop = ews::property(item_property.has_attachments, true);
        EXPECT_THROW(
        {
            service().update_item(minnie.get_item_id(), prop);
        }, ews::exchange_error);
    }

    TEST_F(ServiceTest, UpdateItemOfReadOnlyPropertyThrowsWhatMessage)
    {
        ews::item_property_path item_property;
        auto minnie = test_contact();
        ASSERT_FALSE(minnie.has_attachments());
        auto prop = ews::property(item_property.has_attachments, true);
        try
        {
            service().update_item(minnie.get_item_id(), prop);
            FAIL() << "Expected exception to be thrown";
        }
        catch (ews::exchange_error& exc)
        {
            EXPECT_STREQ("ErrorInvalidPropertySet", exc.what());
        }
    }

    TEST_F(ServiceTest, UpdateItemWithSetItemField)
    {
        // <SetItemField> is used whenever we want to replace an existing value.
        // If none exists yet, it is created

        auto minnie = test_contact();

        EXPECT_STREQ("", minnie.get_spouse_name().c_str());
        auto contact_property = ews::contact_property_path();
        auto spouse_name_property = ews::property(contact_property.spouse_name,
                                                  "Mickey");
        auto new_id =
            service().update_item(minnie.get_item_id(),
                                  spouse_name_property,
                                  ews::conflict_resolution::auto_resolve);
        minnie = service().get_contact(new_id);
        EXPECT_STREQ("Mickey", minnie.get_spouse_name().c_str());

        spouse_name_property = ews::property(contact_property.spouse_name,
                                             "Peg-Leg Pedro");
        new_id =
            service().update_item(minnie.get_item_id(),
                                  spouse_name_property,
                                  ews::conflict_resolution::auto_resolve);
        minnie = service().get_contact(new_id);
        EXPECT_STREQ("Peg-Leg Pedro", minnie.get_spouse_name().c_str());
    }

    TEST_F(ServiceTest, UpdateItemWithDeleteItemField)
    {
        // <DeleteItemField> is simply just a FieldURI to the property that we
        // want to delete. Not much to it. It is automatically deduced by
        // service::update_item if the user has not provided any value to the
        // property he wanted to update

        auto minnie = test_contact();
        ASSERT_FALSE(minnie.get_given_name().empty());
        auto contact_property = ews::contact_property_path();
        auto prop = ews::property(contact_property.given_name);
        auto new_id = service().update_item(minnie.get_item_id(), prop);
        minnie = service().get_contact(new_id);
        EXPECT_TRUE(minnie.get_given_name().empty());

        // FIXME: does not fail but request string contains <SetItemField>,
        // should contain <DeleteItemField> instead
    }

    TEST_F(ServiceTest, UpdateItemWithAppendToItemField)
    {
        // <AppendToItemField> is automatically deduced by service::update_item
        // so user does not need to bother. It is only valid for following
        // properties (at least in EWS 2007 slang):
        //
        // - calendar:OptionalAttendees
        // - calendar:RequiredAttendees
        // - calendar:Resources
        // - item:Body
        // - message:ToRecipients
        // - message:CcRecipients
        // - message:BccRecipients
        // - message:ReplyTo

        auto message_property = ews::message_property_path();

        auto message = ews::message();
        message.set_subject("You are hiding again, aren't you?");
        std::vector<ews::email_address> recipients{
            ews::email_address("darkwing.duck@duckburg.com")
        };
        message.set_to_recipients(recipients);
        auto item_id = service().create_item(
                message,
                ews::message_disposition::save_only);
        on_scope_exit delete_message([&]
        {
            service().delete_message(std::move(message));
        });
        message = service().get_message(item_id);
        recipients = message.get_to_recipients();
        EXPECT_EQ(1U, recipients.size());

        std::vector<ews::email_address> additional_recipients{
            ews::email_address("gus.goose@duckburg.com")
        };
        auto prop = ews::property(message_property.to_recipients,
                                  additional_recipients);
        auto new_id = service().update_item(message.get_item_id(), prop);
        message = service().get_message(item_id);
        ASSERT_EQ(2U, message.get_to_recipients().size());
    }
}
