/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2010 Hemanth Narra
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Hemanth Narra <hemanth@ittc.ku.com>
 *
 * James P.G. Sterbenz <jpgs@ittc.ku.edu>, director
 * ResiliNets Research Group  http://wiki.ittc.ku.edu/resilinets
 * Information and Telecommunication Technology Center (ITTC)
 * and Department of Electrical Engineering and Computer Science
 * The University of Kansas Lawrence, KS USA.
 *
 * Work supported in part by NSF FIND (Future Internet Design) Program
 * under grant CNS-0626918 (Postmodern Internet Architecture),
 * NSF grant CNS-1050226 (Multilayer Network Resilience Analysis and Experimentation on GENI),
 * US Department of Defense (DoD), and ITTC at The University of Kansas.
 */
#include "ns3/test.h"
#include "ns3/mesh-helper.h"
#include "ns3/simulator.h"
#include "ns3/mobility-helper.h"
#include "ns3/nqos-wifi-mac-helper.h"
#include "ns3/leach-helper.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"
#include "ns3/v4ping-helper.h"
#include "ns3/string.h"
#include "ns3/boolean.h"
#include "ns3/yans-wifi-helper.h"
#include "ns3/pcap-file.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/leach-packet.h"
#include "ns3/leach-rtable.h"

using namespace ns3;

class LeachHeaderTestCase : public TestCase
{
public:
  LeachHeaderTestCase ();
  ~LeachHeaderTestCase ();
  virtual void
  DoRun (void);
};
LeachHeaderTestCase::LeachHeaderTestCase ()
  : TestCase ("Verifying the LEACH header")
{
}
LeachHeaderTestCase::~LeachHeaderTestCase ()
{
}
void
LeachHeaderTestCase::DoRun ()
{
  Ptr<Packet> packet = Create<Packet> ();

  {
    leach::LeachHeader hdr1;
    hdr1.SetPosition (65537);
    packet->AddHeader (hdr1);
    leach::LeachHeader hdr2;
    hdr2.SetPosition (196611);
    packet->AddHeader (hdr2);
    NS_TEST_ASSERT_MSG_EQ (packet->GetSize (), 8, "001");
  }

  {
    leach::LeachHeader hdr2;
    packet->RemoveHeader (hdr2);
    NS_TEST_ASSERT_MSG_EQ (hdr2.GetSerializedSize (),4,"002");
    NS_TEST_ASSERT_MSG_EQ (hdr2.GetPosition (), 65537,"003");
    leach::LeachHeader hdr1;
    packet->RemoveHeader (hdr1);
    NS_TEST_ASSERT_MSG_EQ (hdr1.GetSerializedSize (),4,"004");
    NS_TEST_ASSERT_MSG_EQ (hdr1.GetPosition (), 196611,"005");
  }
}

class LeachTableTestCase : public TestCase
{
public:
  LeachTableTestCase ();
  ~LeachTableTestCase ();
  virtual void
  DoRun (void);
};

LeachTableTestCase::LeachTableTestCase ()
  : TestCase ("Leach Routing Table test case")
{
}
LeachTableTestCase::~LeachTableTestCase ()
{
}
void
LeachTableTestCase::DoRun ()
{
  leach::RoutingTable rtable;
  Ptr<NetDevice> dev;
  {
    leach::RoutingTableEntry rEntry1 (
      /*device=*/ dev, /*dst=*/
      Ipv4Address ("10.1.1.4"), /*seqno=*/ 2,
      /*iface=*/ Ipv4InterfaceAddress (Ipv4Address ("10.1.1.1"), Ipv4Mask ("255.255.255.0")),
      /*hops=*/ 2, /*next hop=*/
      Ipv4Address ("10.1.1.2"),
      /*lifetime=*/ Seconds (10));
    NS_TEST_EXPECT_MSG_EQ (rtable.AddRoute (rEntry1),true,"add route");
    leach::RoutingTableEntry rEntry2 (
      /*device=*/ dev, /*dst=*/
      Ipv4Address ("10.1.1.2"), /*seqno=*/ 4,
      /*iface=*/ Ipv4InterfaceAddress (Ipv4Address ("10.1.1.1"), Ipv4Mask ("255.255.255.0")),
      /*hops=*/ 1, /*next hop=*/
      Ipv4Address ("10.1.1.2"),
      /*lifetime=*/ Seconds (10));
    NS_TEST_EXPECT_MSG_EQ (rtable.AddRoute (rEntry2),true,"add route");
    leach::RoutingTableEntry rEntry3 (
      /*device=*/ dev, /*dst=*/
      Ipv4Address ("10.1.1.3"), /*seqno=*/ 4,
      /*iface=*/ Ipv4InterfaceAddress (Ipv4Address ("10.1.1.1"), Ipv4Mask ("255.255.255.0")),
      /*hops=*/ 1, /*next hop=*/
      Ipv4Address ("10.1.1.3"),
      /*lifetime=*/ Seconds (10));
    NS_TEST_EXPECT_MSG_EQ (rtable.AddRoute (rEntry3),true,"add route");
    leach::RoutingTableEntry rEntry4 (
      /*device=*/ dev, /*dst=*/
      Ipv4Address ("10.1.1.255"), /*seqno=*/ 0,
      /*iface=*/ Ipv4InterfaceAddress (Ipv4Address ("10.1.1.1"), Ipv4Mask ("255.255.255.0")),
      /*hops=*/ 0, /*next hop=*/
      Ipv4Address ("10.1.1.255"),
      /*lifetime=*/ Seconds (10));
    NS_TEST_EXPECT_MSG_EQ (rtable.AddRoute (rEntry4),true,"add route");
  }
  {
    leach::RoutingTableEntry rEntry;
    if (rtable.LookupRoute (Ipv4Address ("10.1.1.4"), rEntry))
      {
        NS_TEST_ASSERT_MSG_EQ (rEntry.GetDestination (),Ipv4Address ("10.1.1.4"),"100");
        NS_TEST_ASSERT_MSG_EQ (rEntry.GetSeqNo (),2,"101");
        NS_TEST_ASSERT_MSG_EQ (rEntry.GetHop (),2,"102");
      }
    if (rtable.LookupRoute (Ipv4Address ("10.1.1.2"), rEntry))
      {
        NS_TEST_ASSERT_MSG_EQ (rEntry.GetDestination (),Ipv4Address ("10.1.1.2"),"103");
        NS_TEST_ASSERT_MSG_EQ (rEntry.GetSeqNo (),4,"104");
        NS_TEST_ASSERT_MSG_EQ (rEntry.GetHop (),1,"105");
      }
    if (rtable.LookupRoute (Ipv4Address ("10.1.1.3"), rEntry))
      {
        NS_TEST_ASSERT_MSG_EQ (rEntry.GetDestination (),Ipv4Address ("10.1.1.3"),"106");
        NS_TEST_ASSERT_MSG_EQ (rEntry.GetSeqNo (),4,"107");
        NS_TEST_ASSERT_MSG_EQ (rEntry.GetHop (),1,"108");
      }
    if (rtable.LookupRoute (Ipv4Address ("10.1.1.255"), rEntry))
      {
        NS_TEST_ASSERT_MSG_EQ (rEntry.GetDestination (),Ipv4Address ("10.1.1.255"),"109");
      }
    NS_TEST_ASSERT_MSG_EQ (rEntry.GetInterface ().GetLocal (),Ipv4Address ("10.1.1.1"),"110");
    NS_TEST_ASSERT_MSG_EQ (rEntry.GetInterface ().GetBroadcast (),Ipv4Address ("10.1.1.255"),"111");
    NS_TEST_ASSERT_MSG_EQ (rtable.RoutingTableSize (),4,"Rtable size incorrect");
  }
  Simulator::Destroy ();
}

class LeachTestSuite : public TestSuite
{
public:
  LeachTestSuite () : TestSuite ("routing-leach", UNIT)
  {
    AddTestCase (new LeachHeaderTestCase (), TestCase::QUICK);
    AddTestCase (new LeachTableTestCase (), TestCase::QUICK);
  }
} g_leachTestSuite;
