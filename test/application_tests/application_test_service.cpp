// Copyright (C) 2014-2016 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <chrono>
#include <condition_variable>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <map>
#include <algorithm>

#include <gtest/gtest.h>

#include <vsomeip/vsomeip.hpp>
#include "../../implementation/logging/include/logger.hpp"

#include "application_test_globals.hpp"

class application_test_service {
public:
    application_test_service(struct application_test::service_info _service_info) :
            service_info_(_service_info),
            // service with number 1 uses "vsomeipd" as application name
            // this way the same json file can be reused for all local tests
            // including the ones with vsomeipd
            app_(vsomeip::runtime::get()->create_application("service")),
            counter_(0),
            wait_until_registered_(true),
            offer_thread_(std::bind(&application_test_service::run, this)),
            stop_called_(false) {
        app_->init();
        app_->register_state_handler(
                std::bind(&application_test_service::on_state, this,
                        std::placeholders::_1));

        app_->register_message_handler(service_info_.service_id,
                service_info_.instance_id, service_info_.method_id,
                std::bind(&application_test_service::on_request, this,
                        std::placeholders::_1));

        app_->register_message_handler(service_info_.service_id,
                service_info_.instance_id, service_info_.shutdown_method_id,
                std::bind(&application_test_service::on_shutdown_method_called, this,
                        std::placeholders::_1));
        application_thread_ = std::thread([&](){
            app_->start();
        });
    }

    ~application_test_service() {
        offer_thread_.join();
        application_thread_.join();
    }


    void offer() {
        app_->offer_service(service_info_.service_id, service_info_.instance_id);
    }

    void on_state(vsomeip::state_type_e _state) {
        VSOMEIP_INFO << "Application " << app_->get_name() << " is "
        << (_state == vsomeip::state_type_e::ST_REGISTERED ?
                "registered." : "deregistered.");

        if (_state == vsomeip::state_type_e::ST_REGISTERED) {
            std::lock_guard<std::mutex> its_lock(mutex_);
            wait_until_registered_ = false;
            condition_.notify_one();
        }
    }

    void on_request(const std::shared_ptr<vsomeip::message> &_message) {
        app_->send(vsomeip::runtime::get()->create_response(_message));
        VSOMEIP_INFO << "Received a request with Client/Session [" << std::setw(4)
                << std::setfill('0') << std::hex << _message->get_client() << "/"
                << std::setw(4) << std::setfill('0') << std::hex
                << _message->get_session() << "]";
    }

    void on_shutdown_method_called(const std::shared_ptr<vsomeip::message> &_message) {
        (void)_message;
        stop_called_ = true;
        app_->stop_offer_service(service_info_.service_id, service_info_.instance_id);
        app_->clear_all_handler();
        app_->stop();
    }

    void run() {
        VSOMEIP_DEBUG << "[" << std::setw(4) << std::setfill('0') << std::hex
                << service_info_.service_id << "] Running";
        std::unique_lock<std::mutex> its_lock(mutex_);
        while (wait_until_registered_ && !stop_called_) {
            condition_.wait_for(its_lock, std::chrono::milliseconds(100));
        }

        VSOMEIP_DEBUG << "[" << std::setw(4) << std::setfill('0') << std::hex
                << service_info_.service_id << "] Offering";
        offer();
    }

private:
    struct application_test::service_info service_info_;
    std::shared_ptr<vsomeip::application> app_;
    std::uint32_t counter_;

    bool wait_until_registered_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::thread offer_thread_;
    std::thread application_thread_;
    bool stop_called_;
};
