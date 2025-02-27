// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements. See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership. The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License. You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the License for the
// specific language governing permissions and limitations
// under the License.

use clap::{clap_app, value_t};
use log::*;

use std::collections::{BTreeMap, BTreeSet};
use std::fmt::Debug;
use std::net::{TcpStream, ToSocketAddrs};

#[cfg(unix)]
use std::os::unix::net::UnixStream;
#[cfg(unix)]
use std::path::Path;

use thrift::protocol::{
    TBinaryInputProtocol, TBinaryOutputProtocol, TCompactInputProtocol, TCompactOutputProtocol,
    TInputProtocol, TMultiplexedOutputProtocol, TOutputProtocol,
};
use thrift::transport::{
    TBufferedReadTransport, TBufferedWriteTransport, TFramedReadTransport, TFramedWriteTransport,
    TIoChannel, TReadTransport, TTcpChannel, TWriteTransport,
};
use thrift::OrderedFloat;
use thrift_test::*;

type ThriftClientPair = (
    ThriftTestSyncClient<Box<dyn TInputProtocol>, Box<dyn TOutputProtocol>>,
    Option<SecondServiceSyncClient<Box<dyn TInputProtocol>, Box<dyn TOutputProtocol>>>,
);

fn main() {
    env_logger::init();

    debug!("initialized logger - running cross-test client");

    match run() {
        Ok(()) => info!("cross-test client succeeded"),
        Err(e) => {
            info!("cross-test client failed with error {:?}", e);
            std::process::exit(1);
        }
    }
}

fn run() -> thrift::Result<()> {
    // unsupported options:
    // --pipe
    // --anon-pipes
    // --ssl
    // --threads
    let matches = clap_app!(rust_test_client =>
        (version: "1.0")
        (author: "Apache Thrift Developers <dev@thrift.apache.org>")
        (about: "Rust Thrift test client")
        (@arg host: --host +takes_value "Host on which the Thrift test server is located")
        (@arg port: --port +takes_value "Port on which the Thrift test server is listening")
        (@arg domain_socket: --("domain-socket") +takes_value "Unix Domain Socket on which the Thrift test server is listening")
        (@arg protocol: --protocol +takes_value "Thrift protocol implementation to use (\"binary\", \"compact\", \"multi\", \"multic\")")
        (@arg transport: --transport +takes_value "Thrift transport implementation to use (\"buffered\", \"framed\")")
        (@arg testloops: -n --testloops +takes_value "Number of times to run tests")
    )
        .get_matches();

    let host = matches.value_of("host").unwrap_or("127.0.0.1");
    let port = value_t!(matches, "port", u16).unwrap_or(9090);
    let domain_socket = matches.value_of("domain_socket");
    let protocol = matches.value_of("protocol").unwrap_or("binary");
    let transport = matches.value_of("transport").unwrap_or("buffered");
    let testloops = value_t!(matches, "testloops", u8).unwrap_or(1);

    let (mut thrift_test_client, mut second_service_client) = match domain_socket {
        None => {
            let listen_address = format!("{}:{}", host, port);
            info!(
                "Client binds to {} with {}+{} stack",
                listen_address, protocol, transport
            );
            bind(listen_address.as_str(), protocol, transport)?
        }
        Some(domain_socket) => {
            info!(
                "Client binds to {} (UDS) with {}+{} stack",
                domain_socket, protocol, transport
            );
            bind_uds(domain_socket, protocol, transport)?
        }
    };

    for _ in 0..testloops {
        make_thrift_calls(&mut thrift_test_client, &mut second_service_client)?
    }

    Ok(())
}

fn bind<A: ToSocketAddrs>(
    listen_address: A,
    protocol: &str,
    transport: &str,
) -> Result<ThriftClientPair, thrift::Error> {
    // create a TCPStream that will be shared by all Thrift clients
    // service calls from multiple Thrift clients will be interleaved over the same connection
    // this isn't a problem for us because we're single-threaded and all calls block to completion
    let shared_stream = TcpStream::connect(listen_address)?;

    let second_service_client = if protocol.starts_with("multi") {
        let shared_stream_clone = shared_stream.try_clone()?;
        let channel = TTcpChannel::with_stream(shared_stream_clone);
        let (i_prot, o_prot) = build(channel, transport, protocol, "SecondService")?;
        Some(SecondServiceSyncClient::new(i_prot, o_prot))
    } else {
        None
    };

    let thrift_test_client = {
        let channel = TTcpChannel::with_stream(shared_stream);
        let (i_prot, o_prot) = build(channel, transport, protocol, "ThriftTest")?;
        ThriftTestSyncClient::new(i_prot, o_prot)
    };

    Ok((thrift_test_client, second_service_client))
}

#[cfg(unix)]
fn bind_uds<P: AsRef<Path>>(
    domain_socket: P,
    protocol: &str,
    transport: &str,
) -> Result<ThriftClientPair, thrift::Error> {
    // create a UnixStream that will be shared by all Thrift clients
    // service calls from multiple Thrift clients will be interleaved over the same connection
    // this isn't a problem for us because we're single-threaded and all calls block to completion
    let shared_stream = UnixStream::connect(domain_socket)?;

    let second_service_client = if protocol.starts_with("multi") {
        let shared_stream_clone = shared_stream.try_clone()?;
        let (i_prot, o_prot) = build(shared_stream_clone, transport, protocol, "SecondService")?;
        Some(SecondServiceSyncClient::new(i_prot, o_prot))
    } else {
        None
    };

    let thrift_test_client = {
        let (i_prot, o_prot) = build(shared_stream, transport, protocol, "ThriftTest")?;
        ThriftTestSyncClient::new(i_prot, o_prot)
    };

    Ok((thrift_test_client, second_service_client))
}

fn build<C: TIoChannel + 'static>(
    channel: C,
    transport: &str,
    protocol: &str,
    service_name: &str,
) -> thrift::Result<(Box<dyn TInputProtocol>, Box<dyn TOutputProtocol>)> {
    let (i_chan, o_chan) = channel.split()?;

    let (i_tran, o_tran): (Box<dyn TReadTransport>, Box<dyn TWriteTransport>) = match transport {
        "buffered" => (
            Box::new(TBufferedReadTransport::new(i_chan)),
            Box::new(TBufferedWriteTransport::new(o_chan)),
        ),
        "framed" => (
            Box::new(TFramedReadTransport::new(i_chan)),
            Box::new(TFramedWriteTransport::new(o_chan)),
        ),
        unmatched => return Err(format!("unsupported transport {}", unmatched).into()),
    };

    let (i_prot, o_prot): (Box<dyn TInputProtocol>, Box<dyn TOutputProtocol>) = match protocol {
        "binary" => (
            Box::new(TBinaryInputProtocol::new(i_tran, true)),
            Box::new(TBinaryOutputProtocol::new(o_tran, true)),
        ),
        "multi" => (
            Box::new(TBinaryInputProtocol::new(i_tran, true)),
            Box::new(TMultiplexedOutputProtocol::new(
                service_name,
                TBinaryOutputProtocol::new(o_tran, true),
            )),
        ),
        "compact" => (
            Box::new(TCompactInputProtocol::new(i_tran)),
            Box::new(TCompactOutputProtocol::new(o_tran)),
        ),
        "multic" => (
            Box::new(TCompactInputProtocol::new(i_tran)),
            Box::new(TMultiplexedOutputProtocol::new(
                service_name,
                TCompactOutputProtocol::new(o_tran),
            )),
        ),
        unmatched => return Err(format!("unsupported protocol {}", unmatched).into()),
    };

    Ok((i_prot, o_prot))
}

type BuildThriftTestClient =
    ThriftTestSyncClient<Box<dyn TInputProtocol>, Box<dyn TOutputProtocol>>;
type BuiltSecondServiceClient =
    SecondServiceSyncClient<Box<dyn TInputProtocol>, Box<dyn TOutputProtocol>>;

#[allow(clippy::cognitive_complexity)]
fn make_thrift_calls(
    thrift_test_client: &mut BuildThriftTestClient,
    second_service_client: &mut Option<BuiltSecondServiceClient>,
) -> Result<(), thrift::Error> {
    info!("testVoid");
    thrift_test_client.test_void()?;

    info!("testString");
    verify_expected_result(
        thrift_test_client.test_string("thing".to_owned()),
        "thing".to_owned(),
    )?;

    info!("testUuid");
    verify_expected_result(
        thrift_test_client.test_uuid(uuid::uuid!("00010203-0405-0607-0809-0a0b0c0d0e0f")),
        uuid::uuid!("00010203-0405-0607-0809-0a0b0c0d0e0f"),
    )?;

    info!("testBool");
    verify_expected_result(thrift_test_client.test_bool(true), true)?;

    info!("testBool");
    verify_expected_result(thrift_test_client.test_bool(false), false)?;

    info!("testByte");
    verify_expected_result(thrift_test_client.test_byte(42), 42)?;

    info!("testi32");
    verify_expected_result(thrift_test_client.test_i32(1_159_348_374), 1_159_348_374)?;

    info!("testi64");
    // try!(verify_expected_result(thrift_test_client.test_i64(-8651829879438294565),
    // -8651829879438294565));
    verify_expected_result(thrift_test_client.test_i64(i64::MIN), i64::MIN)?;

    info!("testDouble");
    verify_expected_result(
        thrift_test_client.test_double(OrderedFloat::from(42.42)),
        OrderedFloat::from(42.42),
    )?;

    info!("testTypedef");
    {
        let u_snd: UserId = 2348;
        let u_cmp: UserId = 2348;
        verify_expected_result(thrift_test_client.test_typedef(u_snd), u_cmp)?;
    }

    info!("testEnum");
    {
        verify_expected_result(thrift_test_client.test_enum(Numberz::TWO), Numberz::TWO)?;
    }

    info!("testBinary");
    {
        let b_snd = vec![0x77, 0x30, 0x30, 0x74, 0x21, 0x20, 0x52, 0x75, 0x73, 0x74];
        let b_cmp = vec![0x77, 0x30, 0x30, 0x74, 0x21, 0x20, 0x52, 0x75, 0x73, 0x74];
        verify_expected_result(thrift_test_client.test_binary(b_snd), b_cmp)?;
    }

    info!("testStruct");
    {
        let x_snd = Xtruct {
            string_thing: Some("foo".to_owned()),
            byte_thing: Some(12),
            i32_thing: Some(219_129),
            i64_thing: Some(12_938_492_818),
        };
        let x_cmp = Xtruct {
            string_thing: Some("foo".to_owned()),
            byte_thing: Some(12),
            i32_thing: Some(219_129),
            i64_thing: Some(12_938_492_818),
        };
        verify_expected_result(thrift_test_client.test_struct(x_snd), x_cmp)?;
    }

    // Xtruct again, with optional values
    // FIXME: apparently the erlang thrift server does not like opt-in-req-out
    // parameters that are undefined. Joy.
    // {
    // let x_snd = Xtruct { string_thing: Some("foo".to_owned()), byte_thing: None,
    // i32_thing: None, i64_thing: Some(12938492818) };
    // let x_cmp = Xtruct { string_thing: Some("foo".to_owned()), byte_thing:
    // Some(0), i32_thing: Some(0), i64_thing: Some(12938492818) }; // the C++
    // server is responding correctly
    // try!(verify_expected_result(thrift_test_client.test_struct(x_snd), x_cmp));
    // }
    //

    info!("testNest"); // (FIXME: try Xtruct2 with optional values)
    {
        let x_snd = Xtruct2 {
            byte_thing: Some(32),
            struct_thing: Some(Xtruct {
                string_thing: Some("foo".to_owned()),
                byte_thing: Some(1),
                i32_thing: Some(324_382_098),
                i64_thing: Some(12_938_492_818),
            }),
            i32_thing: Some(293_481_098),
        };
        let x_cmp = Xtruct2 {
            byte_thing: Some(32),
            struct_thing: Some(Xtruct {
                string_thing: Some("foo".to_owned()),
                byte_thing: Some(1),
                i32_thing: Some(324_382_098),
                i64_thing: Some(12_938_492_818),
            }),
            i32_thing: Some(293_481_098),
        };
        verify_expected_result(thrift_test_client.test_nest(x_snd), x_cmp)?;
    }

    // do the multiplexed calls while making the main ThriftTest calls
    if let Some(ref mut client) = second_service_client.as_mut() {
        info!("SecondService secondtestString");
        {
            verify_expected_result(
                client.secondtest_string("test_string".to_owned()),
                "testString(\"test_string\")".to_owned(),
            )?;
        }
    }

    info!("testList");
    {
        let v_snd: Vec<i32> = vec![29384, 238, 32498];

        let v_cmp: Vec<i32> = vec![29384, 238, 32498];

        verify_expected_result(thrift_test_client.test_list(v_snd), v_cmp)?;
    }

    info!("testSet");
    {
        let s_snd: BTreeSet<i32> = BTreeSet::from([293_481, 23, 3234]);

        let s_cmp: BTreeSet<i32> = BTreeSet::from([293_481, 23, 3234]);

        verify_expected_result(thrift_test_client.test_set(s_snd), s_cmp)?;
    }

    info!("testMap");
    {
        let m_snd: BTreeMap<i32, i32> = BTreeMap::from([(2, 4), (4, 6), (8, 7)]);

        let m_cmp: BTreeMap<i32, i32> = BTreeMap::from([(2, 4), (4, 6), (8, 7)]);

        verify_expected_result(thrift_test_client.test_map(m_snd), m_cmp)?;
    }

    info!("testStringMap");
    {
        let m_snd: BTreeMap<String, String> = BTreeMap::from([
            ("2".to_owned(), "4_string".to_owned()),
            ("4".to_owned(), "6_string".to_owned()),
            ("8".to_owned(), "7_string".to_owned()),
        ]);

        let m_rcv: BTreeMap<String, String> = BTreeMap::from([
            ("2".to_owned(), "4_string".to_owned()),
            ("4".to_owned(), "6_string".to_owned()),
            ("8".to_owned(), "7_string".to_owned()),
        ]);

        verify_expected_result(thrift_test_client.test_string_map(m_snd), m_rcv)?;
    }

    // nested map
    // expect : {-4 => {-4 => -4, -3 => -3, -2 => -2, -1 => -1, }, 4 => {1 => 1, 2
    // => 2, 3 => 3, 4 => 4, }, }
    info!("testMapMap");
    {
        let m_cmp_nested_0: BTreeMap<i32, i32> = (-4..0).map(|i| (i, i)).collect();
        let m_cmp_nested_1: BTreeMap<i32, i32> = (1..5).map(|i| (i, i)).collect();

        let m_cmp: BTreeMap<i32, BTreeMap<i32, i32>> =
            BTreeMap::from([(-4, m_cmp_nested_0), (4, m_cmp_nested_1)]);

        verify_expected_result(thrift_test_client.test_map_map(42), m_cmp)?;
    }

    info!("testMulti");
    {
        let m_snd: BTreeMap<i16, String> =
            BTreeMap::from([(1298, "fizz".to_owned()), (-148, "buzz".to_owned())]);

        let s_cmp = Xtruct {
            string_thing: Some("Hello2".to_owned()),
            byte_thing: Some(1),
            i32_thing: Some(-123_948),
            i64_thing: Some(-19_234_123_981),
        };

        verify_expected_result(
            thrift_test_client.test_multi(1, -123_948, -19_234_123_981, m_snd, Numberz::EIGHT, 81),
            s_cmp,
        )?;
    }

    // Insanity
    // returns:
    // { 1 => { 2 => argument,
    //          3 => argument,
    //        },
    //   2 => { 6 => <empty Insanity struct>, },
    // }
    {
        let arg_map_usermap: BTreeMap<Numberz, i64> =
            BTreeMap::from([(Numberz::ONE, 4289), (Numberz::EIGHT, 19)]);

        let arg_vec_xtructs: Vec<Xtruct> = vec![
            Xtruct {
                string_thing: Some("foo".to_owned()),
                byte_thing: Some(8),
                i32_thing: Some(29),
                i64_thing: Some(92384),
            },
            Xtruct {
                string_thing: Some("bar".to_owned()),
                byte_thing: Some(28),
                i32_thing: Some(2),
                i64_thing: Some(-1281),
            },
            Xtruct {
                string_thing: Some("baz".to_owned()),
                byte_thing: Some(0),
                i32_thing: Some(3_948_539),
                i64_thing: Some(-12_938_492),
            },
        ];

        let insanity = Insanity {
            user_map: Some(arg_map_usermap),
            xtructs: Some(arg_vec_xtructs),
        };
        let s_cmp_nested_1: BTreeMap<Numberz, Insanity> = BTreeMap::from([
            (Numberz::TWO, insanity.clone()),
            (Numberz::THREE, insanity.clone()),
        ]);

        let empty_insanity = Insanity {
            user_map: Some(BTreeMap::new()),
            xtructs: Some(Vec::new()),
        };
        let s_cmp_nested_2: BTreeMap<Numberz, Insanity> =
            BTreeMap::from([(Numberz::SIX, empty_insanity)]);

        let s_cmp: BTreeMap<UserId, BTreeMap<Numberz, Insanity>> =
            BTreeMap::from([(1, s_cmp_nested_1), (2, s_cmp_nested_2)]);

        verify_expected_result(thrift_test_client.test_insanity(insanity), s_cmp)?;
    }

    info!("testException - remote throws Xception");
    {
        let r = thrift_test_client.test_exception("Xception".to_owned());
        let x = match r {
            Err(thrift::Error::User(ref e)) => match e.downcast_ref::<Xception>() {
                Some(x) => Ok(x),
                None => Err(thrift::Error::User(
                    "did not get expected Xception struct".into(),
                )),
            },
            _ => Err(thrift::Error::User("did not get exception".into())),
        }?;

        let x_cmp = Xception {
            error_code: Some(1001),
            message: Some("Xception".to_owned()),
        };

        verify_expected_result(Ok(x), &x_cmp)?;
    }

    info!("testException - remote throws TApplicationException");
    {
        let r = thrift_test_client.test_exception("TException".to_owned());
        match r {
            Err(thrift::Error::Application(ref e)) => {
                info!("received an {:?}", e);
                Ok(())
            }
            _ => Err(thrift::Error::User("did not get exception".into())),
        }?;
    }

    info!("testException - remote succeeds");
    {
        let r = thrift_test_client.test_exception("foo".to_owned());
        match r {
            Ok(_) => Ok(()),
            _ => Err(thrift::Error::User("received an exception".into())),
        }?;
    }

    info!("testMultiException - remote throws Xception");
    {
        let r =
            thrift_test_client.test_multi_exception("Xception".to_owned(), "ignored".to_owned());
        let x = match r {
            Err(thrift::Error::User(ref e)) => match e.downcast_ref::<Xception>() {
                Some(x) => Ok(x),
                None => Err(thrift::Error::User(
                    "did not get expected Xception struct".into(),
                )),
            },
            _ => Err(thrift::Error::User("did not get exception".into())),
        }?;

        let x_cmp = Xception {
            error_code: Some(1001),
            message: Some("This is an Xception".to_owned()),
        };

        verify_expected_result(Ok(x), &x_cmp)?;
    }

    info!("testMultiException - remote throws Xception2");
    {
        let r =
            thrift_test_client.test_multi_exception("Xception2".to_owned(), "ignored".to_owned());
        let x = match r {
            Err(thrift::Error::User(ref e)) => match e.downcast_ref::<Xception2>() {
                Some(x) => Ok(x),
                None => Err(thrift::Error::User(
                    "did not get expected Xception struct".into(),
                )),
            },
            _ => Err(thrift::Error::User("did not get exception".into())),
        }?;

        let x_cmp = Xception2 {
            error_code: Some(2002),
            struct_thing: Some(Xtruct {
                string_thing: Some("This is an Xception2".to_owned()),
                // since this is an OPT_IN_REQ_OUT field the sender sets a default
                byte_thing: Some(0),
                // since this is an OPT_IN_REQ_OUT field the sender sets a default
                i32_thing: Some(0),
                // since this is an OPT_IN_REQ_OUT field the sender sets a default
                i64_thing: Some(0),
            }),
        };

        verify_expected_result(Ok(x), &x_cmp)?;
    }

    info!("testMultiException - remote succeeds");
    {
        let r = thrift_test_client.test_multi_exception("haha".to_owned(), "RETURNED".to_owned());
        let x = match r {
            Err(e) => Err(thrift::Error::User(
                format!("received an unexpected exception {:?}", e).into(),
            )),
            _ => r,
        }?;

        let x_cmp = Xtruct {
            string_thing: Some("RETURNED".to_owned()),
            // since this is an OPT_IN_REQ_OUT field the sender sets a default
            byte_thing: Some(0),
            // since this is an OPT_IN_REQ_OUT field the sender sets a default
            i32_thing: Some(0),
            // since this is an OPT_IN_REQ_OUT field the sender sets a default
            i64_thing: Some(0),
        };

        verify_expected_result(Ok(x), x_cmp)?;
    }

    info!("testOneWay - remote sleeps for 1 second");
    {
        thrift_test_client.test_oneway(1)?;
    }

    // final test to verify that the connection is still writable after the one-way
    // call
    thrift_test_client.test_void()
}

fn verify_expected_result<T: Debug + PartialEq + Sized>(
    actual: Result<T, thrift::Error>,
    expected: T,
) -> Result<(), thrift::Error> {
    info!("*** EXPECTED: Ok({:?})", expected);
    info!("*** ACTUAL  : {:?}", actual);
    match actual {
        Ok(v) => {
            if v == expected {
                info!("*** OK ***");
                Ok(())
            } else {
                info!("*** FAILED ***");
                Err(thrift::Error::User(
                    format!("expected {:?} but got {:?}", &expected, &v).into(),
                ))
            }
        }
        Err(e) => Err(e),
    }
}
