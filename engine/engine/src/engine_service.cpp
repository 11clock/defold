#include <stdint.h>
#include <string.h>
#include <dlib/web_server.h>
#include <dlib/message.h>
#include <dlib/dstrings.h>
#include <dlib/math.h>
#include <dlib/log.h>
#include <dlib/ssdp.h>
#include <dlib/socket.h>
#include <dlib/template.h>
#include <ddf/ddf.h>
#include "engine_service.h"

static const char DEVICE_DESC_TEMPLATE[] =
"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
"<root xmlns=\"urn:schemas-upnp-org:device-1-0\" xmlns:defold=\"urn:schemas-defold-com:DEFOLD-1-0\">\n"
"    <specVersion>\n"
"        <major>1</major>\n"
"        <minor>0</minor>\n"
"    </specVersion>\n"
"    <device>\n"
"        <deviceType>upnp:rootdevice</deviceType>\n"
"        <friendlyName>${NAME}</friendlyName>\n"
"        <manufacturer>Defold</manufacturer>\n"
"        <modelName>Defold Engine 1.0</modelName>\n"
"        <UDN>${UDN}</UDN>\n"
"        <defold:url>http://${HOSTNAME}:${DEFOLD_PORT}</defold:url>\n"
"        <defold:logPort>${DEFOLD_LOG_PORT}</defold:logPort>\n"
"    </device>\n"
"</root>\n";

namespace dmEngineService
{
    struct EngineService
    {
        static void HttpServerHeader(void* user_data, const char* key, const char* value)
        {
            (void) user_data;
            (void) key;
            (void) value;
        }

        static bool ParsePostUrl(const char* resource, dmMessage::HSocket* socket, const dmDDF::Descriptor** desc, dmhash_t* message_id)
        {
            // Syntax: http://host:port/post/socket/message_type

            char buf[256];
            dmStrlCpy(buf, resource, sizeof(buf));

            char* last;
            int i = 0;
            char* s = dmStrTok(buf, "/", &last);
            bool error = false;

            while (s && !error)
            {
                switch (i)
                {
                    case 0:
                    {
                        if (strcmp(s, "post") != 0)
                        {
                            error = true;
                        }
                    }
                    break;

                    case 1:
                    {
                        dmMessage::Result mr = dmMessage::GetSocket(s, socket);
                        if (mr != dmMessage::RESULT_OK)
                        {
                            error = true;
                        }
                    }
                    break;

                    case 2:
                    {
                        *message_id = dmHashString64(s);
                        *desc = dmDDF::GetDescriptorFromHash(*message_id);
                        if (*desc == 0)
                        {
                            error = true;
                        }
                    }
                    break;
                }

                s = dmStrTok(0, "/", &last);
                ++i;
            }

            return !error;
        }

        static void SlurpHttpContent(dmWebServer::Request* request)
        {
            char buf[256];
            uint32_t total_recv = 0;

            while (total_recv < request->m_ContentLength)
            {
                uint32_t recv_bytes = 0;
                uint32_t to_read = dmMath::Min((uint32_t) sizeof(buf), request->m_ContentLength - total_recv);
                dmWebServer::Result r = dmWebServer::Receive(request, buf, to_read, &recv_bytes);
                if (r != dmWebServer::RESULT_OK)
                    return;
                total_recv += recv_bytes;
            }
        }

        static void PostHandler(void* user_data, dmWebServer::Request* request)
        {
            char msg_buf[1024];
            const char* error_msg = "";
            dmWebServer::Result r;
            uint32_t recv_bytes = 0;
            dmMessage::HSocket socket = 0;
            const dmDDF::Descriptor* desc = 0;
            dmhash_t message_id;

            if (request->m_ContentLength > sizeof(msg_buf))
            {
                error_msg = "Too large message";
                goto bail;
            }

            if (!ParsePostUrl(request->m_Resource, &socket, &desc, &message_id))
            {
                error_msg = "Invalid request";
                goto bail;
            }

            r = dmWebServer::Receive(request, msg_buf, request->m_ContentLength, &recv_bytes);
            if (r == dmWebServer::RESULT_OK)
            {
                void* msg;
                uint32_t msg_size;
                dmDDF::Result ddf_r = dmDDF::LoadMessage(msg_buf, recv_bytes, desc, &msg, dmDDF::OPTION_OFFSET_STRINGS, &msg_size);
                if (ddf_r == dmDDF::RESULT_OK)
                {
                    dmMessage::URL url;
                    url.m_Socket = socket;
                    url.m_Path = 0;
                    url.m_Fragment = 0;
                    dmMessage::Post(0, &url, message_id, 0, (uintptr_t) desc, msg, msg_size);
                    dmDDF::FreeMessage(msg);
                }
            }
            else
            {
                dmLogError("Error while reading message post data (%d)", r);
                error_msg = "Internal error";
                goto bail;
            }

            dmWebServer::SetStatusCode(request, 200);
            dmWebServer::Send(request, "OK", strlen("OK"));
            return;

    bail:
            SlurpHttpContent(request);
            dmLogError("%s", error_msg);
            dmWebServer::SetStatusCode(request, 400);
            dmWebServer::Send(request, error_msg, strlen(error_msg));
        }

        static void PingHandler(void* user_data, dmWebServer::Request* request)
        {
            dmWebServer::SetStatusCode(request, 200);
            dmWebServer::Send(request, "PONG\n", strlen("PONG\n"));
            return;
        }

        static const char* ReplaceCallback(void* user_data, const char* key)
        {
            EngineService* self = (EngineService*) user_data;
            if (strcmp(key, "UDN") == 0)
            {
                return self->m_DeviceDesc.m_UDN;
            }
            else if (strcmp(key, "DEFOLD_PORT") == 0)
            {
                return self->m_PortText;
            }
            else if (strcmp(key, "DEFOLD_LOG_PORT") == 0)
            {
                return self->m_LogPortText;
            }
            else if (strcmp(key, "NAME") == 0)
            {
                return self->m_Hostname;
            }
            else if (strcmp(key, "HOSTNAME") == 0)
            {
                return self->m_LocalAddress;
            }
            else
            {
                return 0;
            }
        }

        bool Init(uint16_t port)
        {
            dmSocket::Result sockr = dmSocket::GetHostname(m_Hostname, sizeof(m_Hostname));
            if (sockr != dmSocket::RESULT_OK)
            {
                return false;
            }

            dmSocket::Address local_address;
            sockr = dmSocket::GetLocalAddress(&local_address);
            if (sockr != dmSocket::RESULT_OK)
            {
                return false;
            }
            char* local_address_str =  dmSocket::AddressToIPString(local_address);
            dmStrlCpy(m_LocalAddress, local_address_str, sizeof(m_LocalAddress));
            free(local_address_str);

            dmWebServer::NewParams params;
            params.m_Port = port;
            dmWebServer::HServer web_server;
            dmWebServer::Result r = dmWebServer::New(&params, &web_server);
            if (r != dmWebServer::RESULT_OK)
            {
                dmLogError("Unable to create engine web-server (%d)", r);
                return false;
            }
            dmSocket::Address address;
            dmWebServer::GetName(web_server, &address, &m_Port);
            DM_SNPRINTF(m_PortText, sizeof(m_PortText), "%d", (int) m_Port);
            DM_SNPRINTF(m_LogPortText, sizeof(m_LogPortText), "%d", (int) dmLogGetPort());

            dmStrlCpy(m_DeviceDesc.m_UDN, "defold-", sizeof(m_DeviceDesc.m_UDN));
            char hostname[128];
            dmSocket::GetHostname(hostname, sizeof(hostname));
            dmStrlCat(m_DeviceDesc.m_UDN, hostname, sizeof(m_DeviceDesc.m_UDN));

            dmTemplate::Format(this, m_DeviceDescXml, sizeof(m_DeviceDescXml), DEVICE_DESC_TEMPLATE, ReplaceCallback);

            m_DeviceDesc.m_Id = "defold";
            m_DeviceDesc.m_DeviceType = "upnp:rootdevice";
            m_DeviceDesc.m_DeviceDescription = m_DeviceDescXml;

            dmSSDP::NewParams ssdp_params;
            dmSSDP::HSSDP ssdp;
            dmSSDP::Result sr = dmSSDP::New(&ssdp_params, &ssdp);
            if (sr != dmSSDP::RESULT_OK)
            {
                dmLogError("Unable to create ssdp service (%d)", sr);
                dmWebServer::Delete(web_server);
                return false;
            }

            sr = dmSSDP::RegisterDevice(ssdp, &m_DeviceDesc);
            if (sr != dmSSDP::RESULT_OK)
            {
                dmWebServer::Delete(web_server);
                dmSSDP::Delete(ssdp);
                dmLogError("Unable to register ssdp device (%d)", sr);
                return false;
            }

            dmWebServer::HandlerParams post_params;
            post_params.m_Handler = PostHandler;
            post_params.m_Userdata = this;
            dmWebServer::AddHandler(web_server, "/post", &post_params);

            dmWebServer::HandlerParams ping_params;
            ping_params.m_Handler = PingHandler;
            ping_params.m_Userdata = this;
            dmWebServer::AddHandler(web_server, "/ping", &ping_params);

            m_WebServer = web_server;
            m_SSDP = ssdp;
            return true;
        }

        void Final()
        {
            dmWebServer::Delete(m_WebServer);
            dmSSDP::DeregisterDevice(m_SSDP, "defold");
            dmSSDP::Delete(m_SSDP);
        }


        dmWebServer::HServer m_WebServer;
        uint16_t             m_Port;
        char                 m_PortText[16];
        char                 m_LogPortText[16];
        char                 m_Hostname[128];
        char                 m_LocalAddress[128];

        dmSSDP::DeviceDesc   m_DeviceDesc;
        char                 m_DeviceDescXml[sizeof(DEVICE_DESC_TEMPLATE) + 512]; // 512 is rather arbitrary :-)
        dmSSDP::HSSDP        m_SSDP;
    };

    HEngineService New(uint16_t port)
    {
        HEngineService service = new EngineService();
        if (service->Init(port))
        {
            return service;
        }
        else
        {
            delete service;
            return 0;
        }
    }

    void Delete(HEngineService engine_service)
    {
        engine_service->Final();
        delete engine_service;
    }

    void Update(HEngineService engine_service)
    {
        dmWebServer::Update(engine_service->m_WebServer);
        dmSSDP::Update(engine_service->m_SSDP, false);
    }

    uint16_t GetPort(HEngineService engine_service)
    {
        return engine_service->m_Port;
    }

}

