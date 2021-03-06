/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2010 Hemanth Narra, Yufei Cheng
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
 * Author: Yufei Cheng   <yfcheng@ittc.ku.edu>
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

#include "leach-routing-protocol.h"
#include "ns3/log.h"
#include "ns3/inet-socket-address.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/wifi-net-device.h"
#include "ns3/boolean.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"
#include "ns3/vector.h"
#include "ns3/udp-header.h"

#include <iostream>
#include <cmath>
#include <vector>

//#define DA
//#define DA_PROP
//#define DA_OPT
//#define DA_CL
//#define DA_SF


namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("LeachRoutingProtocol");
  
namespace leach {
  
NS_OBJECT_ENSURE_REGISTERED (RoutingProtocol);

/// UDP Port for LEACH control traffic
const uint32_t RoutingProtocol::LEACH_PORT = 269;

double max(double a, double b) {
    return (a>b)?a:b;
}
  
TypeId
RoutingProtocol::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::leach::RoutingProtocol")
    .SetParent<Ipv4RoutingProtocol> ()
    .SetGroupName ("Leach")
    .AddConstructor<RoutingProtocol> ()
    .AddAttribute ("PeriodicUpdateInterval","Periodic interval between exchange of full routing tables among nodes. ",
                   TimeValue (Seconds (15)),
                   MakeTimeAccessor (&RoutingProtocol::m_periodicUpdateInterval),
                   MakeTimeChecker ())
    .AddAttribute ("Position", "X and Y position of the node",
                   Vector3DValue (),
                   MakeVectorAccessor (&RoutingProtocol::m_position),
                   MakeVectorChecker ())
    .AddAttribute ("Lambda", "Average Packet generation rate",
                   DoubleValue (1.0),
                   MakeDoubleAccessor (&RoutingProtocol::m_lambda),
                   MakeDoubleChecker <double>())
    .AddTraceSource ("DroppedCount", "Total packets dropped",
                   MakeTraceSourceAccessor (&RoutingProtocol::m_dropped),
                   "ns3::TracedValueCallback::Uint32")
    ;
  return tid;
}

void
RoutingProtocol::SetPosition (Vector f)
{
  m_position = f;
}
Vector
RoutingProtocol::GetPosition () const
{
  return m_position;
}
std::vector<struct msmt>*
RoutingProtocol::getTimeline()
{
  return &timeline;
}
std::vector<Time>*
RoutingProtocol::getTxTime()
{
  return &tx_time;
}
  
int64_t
RoutingProtocol::AssignStreams (int64_t stream)
{
  NS_LOG_FUNCTION (this << stream);
  m_uniformRandomVariable->SetStream (stream);
  return 1;
}

RoutingProtocol::RoutingProtocol ()
  : Round(0),
    isSink(0),
    m_dropped (0),
    m_lambda (4.0),
	timeline(),
	tx_time(),
    m_routingTable (),
    m_bestRoute(),
    m_queue (),
    m_periodicUpdateTimer (Timer::CANCEL_ON_DESTROY),
    m_broadcastClusterHeadTimer (Timer::CANCEL_ON_DESTROY),
    m_respondToClusterHeadTimer (Timer::CANCEL_ON_DESTROY)
{
  m_uniformRandomVariable = CreateObject<UniformRandomVariable> ();
  for(int i=0; i<1021; i++) m_hash[i] = NULL;
}

RoutingProtocol::~RoutingProtocol ()
{
}

void
RoutingProtocol::DoDispose ()
{
  m_ipv4 = 0;
  for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::iterator iter = m_socketAddresses.begin (); iter
       != m_socketAddresses.end (); iter++)
    {
      iter->first->Close ();
    }
  m_socketAddresses.clear ();
  Ipv4RoutingProtocol::DoDispose ();
}

void
RoutingProtocol::PrintRoutingTable (Ptr<OutputStreamWrapper> stream) const
{
  *stream->GetStream () << "Node: " << m_ipv4->GetObject<Node> ()->GetId ()
                        << ", Time: " << Now().As (Time::S)
                        << ", Local time: " << GetObject<Node> ()->GetLocalTime ().As (Time::S)
                        << ", LEACH Routing table" << std::endl;

  m_routingTable.Print (stream);
  *stream->GetStream () << std::endl;
}

void
RoutingProtocol::Start ()
{
  m_scb = MakeCallback (&RoutingProtocol::Send,this);
  m_ecb = MakeCallback (&RoutingProtocol::Drop,this);
  m_sinkAddress = Ipv4Address("10.1.1.1");
  ns3::PacketMetadata::Enable ();
  ns3::Packet::EnablePrinting ();
  
  if(m_mainAddress == m_sinkAddress) {
    isSink = 1;
  } else {
    Round = 0;
    m_routingTable.Setholddowntime (Time (m_periodicUpdateInterval));
    m_periodicUpdateTimer.SetFunction (&RoutingProtocol::PeriodicUpdate,this);
    m_broadcastClusterHeadTimer.SetFunction (&RoutingProtocol::SendBroadcast,this);
    m_respondToClusterHeadTimer.SetFunction(&RoutingProtocol::RespondToClusterHead, this);
    m_periodicUpdateTimer.Schedule (MicroSeconds (m_uniformRandomVariable->GetInteger (10,1000)));
  }
}

Ptr<Ipv4Route>
RoutingProtocol::RouteOutput (Ptr<Packet> p,
                              const Ipv4Header &header,
                              Ptr<NetDevice> oif,
                              Socket::SocketErrno &sockerr)
{
  NS_LOG_FUNCTION (this << header << (oif ? oif->GetIfIndex () : 0));

  if (m_socketAddresses.empty ())
    {
      sockerr = Socket::ERROR_NOROUTETOHOST;
      NS_LOG_LOGIC ("No leach interfaces");
      Ptr<Ipv4Route> route;
      return route;
    }

  Ipv4Address dst = header.GetDestination ();
  RoutingTableEntry rt;
  NS_LOG_DEBUG ("Packet Size: " << p->GetSize ()
                                << ", Packet id: " << p->GetUid () << ", Destination address in Packet: " << dst);
#ifdef DA
  if (p->GetSize()%56 == 0)
    {
/*
      if (p->GetSize() == 56)
	    {
          Ptr<Packet> packet = new Packet(*p);
          LeachHeader hdr;
          struct ns3::leach::msmt tmp;
          
          packet->RemoveHeader(hdr);
          tmp.begin = Simulator::Now();
          tmp.end = hdr.GetDeadline();
          timeline.push_back(tmp);
		}
*/
      if (DataAggregation (p))
        {
#endif
          if (m_routingTable.LookupRoute (dst,rt))
            {
              tx_time.push_back(Simulator::Now());
				
              Ptr<Packet> packet = new Packet(*p);
              LeachHeader hdr;
              struct ns3::leach::msmt tmp;
          
              packet->RemoveHeader(hdr);
              tmp.begin = Simulator::Now();
              tmp.end = hdr.GetDeadline();
              timeline.push_back(tmp);
				
              return rt.GetRoute();
            }
#ifdef DA
        }
    }
  else if (m_routingTable.LookupRoute (dst,rt))
    {
//      tx_time.push_back(Simulator::Now());
      return rt.GetRoute();
    }
#endif

  return LoopbackRoute (header,oif);
}

Ptr<Ipv4Route>
RoutingProtocol::LoopbackRoute (const Ipv4Header & hdr, Ptr<NetDevice> oif) const
{
  NS_ASSERT (m_lo != 0);
  NS_LOG_DEBUG("");
  Ptr<Ipv4Route> rt = Create<Ipv4Route> ();
  rt->SetDestination (hdr.GetDestination ());
  
//  NS_LOG_DEBUG("");
  
  // rt->SetSource (hdr.GetSource ());
  //
  // Source address selection here is tricky.  The loopback route is
  // returned when LEACH does not have a route; this causes the packet
  // to be looped back and handled (cached) in RouteInput() method
  // while a route is found. However, connection-oriented protocols
  // like TCP need to create an endpoint four-tuple (src, src port,
  // dst, dst port) and create a pseudo-header for checksumming.  So,
  // LEACH needs to guess correctly what the eventual source address
  // will be.
  //
  // For single interface, single address nodes, this is not a problem.
  // When there are possibly multiple outgoing interfaces, the policy
  // implemented here is to pick the first available LEACH interface.
  // If RouteOutput() caller specified an outgoing interface, that
  // further constrains the selection of source address
  //
  std::map<Ptr<Socket>, Ipv4InterfaceAddress>::const_iterator j = m_socketAddresses.begin ();
  if (oif)
    {
      // Iterate to find an address on the oif device
      for (j = m_socketAddresses.begin (); j != m_socketAddresses.end (); ++j)
        {
          Ipv4Address addr = j->second.GetLocal ();
          int32_t interface = m_ipv4->GetInterfaceForAddress (addr);
          if (oif == m_ipv4->GetNetDevice (static_cast<uint32_t> (interface)))
            {
              rt->SetSource (addr);
              break;
            }
        }
    }
  else
    {
      rt->SetSource (j->second.GetLocal ());
    }
  NS_ASSERT_MSG (rt->GetSource () != Ipv4Address (), "Valid LEACH source address not found");
  rt->SetGateway (Ipv4Address ("127.0.0.1"));
  rt->SetOutputDevice (m_lo);
  return rt;
}

bool
RoutingProtocol::RouteInput (Ptr<const Packet> p,
                             const Ipv4Header &header,
                             Ptr<const NetDevice> idev,
                             UnicastForwardCallback ucb,
                             MulticastForwardCallback mcb,
                             LocalDeliverCallback lcb,
                             ErrorCallback ecb)
{
  NS_LOG_FUNCTION (m_mainAddress << " received packet " << p->GetUid ()
                                 << " from " << header.GetSource ()
                                 << " on interface " << idev->GetAddress ()
                                 << " to destination " << header.GetDestination ());
  if (m_socketAddresses.empty ())
    {
      NS_LOG_DEBUG ("No leach interfaces");
      return false;
    }
  NS_ASSERT (m_ipv4 != 0);
  // Check if input device supports IP
  NS_ASSERT (m_ipv4->GetInterfaceForDevice (idev) >= 0);
  int32_t iif = m_ipv4->GetInterfaceForDevice (idev);

  Ipv4Address dst = header.GetDestination ();
  Ipv4Address origin = header.GetSource ();

  // LEACH is not a multicast routing protocol
  if (dst.IsMulticast ())
    {
      return false;
    }

  // Deferred route request
  if (idev == m_lo)
    {
      NS_LOG_DEBUG("LoopBackRoute");
#ifdef DA
      Ptr<Packet> pa = new Packet(*p);
      EnqueuePacket (pa,header);
      return false;
#else
      RoutingTableEntry toDst;
      NS_LOG_DEBUG("Deferred: " << dst);
      
      if (m_routingTable.LookupRoute (dst,toDst))
        {
              Ptr<Ipv4Route> route = toDst.GetRoute ();
              NS_LOG_DEBUG("Deferred forwarding");
              NS_LOG_DEBUG("Src: " << route->GetSource() << ", Dst: " << toDst.GetDestination() << ", Gateway: " << toDst.GetNextHop());
              ucb(route, p ,header);
        }
      else
        {
          NS_LOG_DEBUG("Route not found");
          
          Ptr<Ipv4Route> rt = Create<Ipv4Route> ();
          rt->SetDestination (dst);
          rt->SetSource (origin);
          rt->SetGateway (Ipv4Address ("127.0.0.1"));
          rt->SetOutputDevice (m_lo);
          
          EnqueueForNoDA(ucb, rt, p, header);
        }
      return true;
#endif
    }
  for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::const_iterator j =
         m_socketAddresses.begin (); j != m_socketAddresses.end (); ++j)
    {
      Ipv4InterfaceAddress iface = j->second;
      if (origin == iface.GetLocal ())
        {
          return true;
        }
    }
  // LOCAL DELIVARY TO LEACH INTERFACES
  for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::const_iterator j = m_socketAddresses.begin (); j
       != m_socketAddresses.end (); ++j)
    {
      Ipv4InterfaceAddress iface = j->second;
      if (m_ipv4->GetInterfaceForAddress (iface.GetLocal ()) == iif)
        {
          // Do not deal with broadcast
          if (dst == iface.GetBroadcast () || dst.IsBroadcast ())
            {
              Ptr<Packet> packet = p->Copy ();
              if (lcb.IsNull () == false)
                {
                  NS_LOG_LOGIC ("Broadcast local delivery to " << iface.GetLocal ());
                  lcb (p, header, iif);
                  // Fall through to additional processing
                }
              else
                {
                  NS_LOG_ERROR ("Unable to deliver packet locally due to null callback " << p->GetUid () << " from " << origin);
                  ecb (p, header, Socket::ERROR_NOROUTETOHOST);
                }
              if (header.GetTtl () > 1)
                {
                  NS_LOG_LOGIC ("Forward broadcast. TTL " << (uint16_t) header.GetTtl ());
                  RoutingTableEntry toBroadcast;
                  if (m_routingTable.LookupRoute (dst,toBroadcast,true))
                    {
                      Ptr<Ipv4Route> route = toBroadcast.GetRoute ();
                      ucb (route,packet,header);
                    }
                  else
                    {
                      NS_LOG_DEBUG ("No route to forward. Drop packet " << p->GetUid ());
                    }
                }
              return true;
            }
        }
    }

  // this means arrival
  if (m_ipv4->IsDestinationAddress (dst, iif))
    {
      if (lcb.IsNull () == false)
        {
          NS_LOG_LOGIC ("Unicast local delivery to " << dst);
          lcb (p, header, iif);
        }
      else
        {
          NS_LOG_ERROR ("Unable to deliver packet locally due to null callback " << p->GetUid () << " from " << origin);
          ecb (p, header, Socket::ERROR_NOROUTETOHOST);
        }
      return true;
    }

  // Check if input device supports IP forwarding
  if (m_ipv4->IsForwarding (iif) == false)
    {
      NS_LOG_LOGIC ("Forwarding disabled for this interface");
      ecb (p, header, Socket::ERROR_NOROUTETOHOST);
      return true;
    }

  // enqueue this and not send
  RoutingTableEntry toDst;
  if (m_routingTable.LookupRoute (dst,toDst))
    {
      RoutingTableEntry ne;
      if (m_routingTable.LookupRoute (toDst.GetNextHop (),ne))
        {
          Ptr<Ipv4Route> route = ne.GetRoute ();
          NS_LOG_LOGIC (m_mainAddress << " is forwarding packet " << p->GetUid ()
                                      << " to " << dst
                                      << " from " << header.GetSource ()
                                      << " via nexthop neighbor " << toDst.GetNextHop ());

#ifdef DA
          Ptr<Packet> pa = new Packet(*p);
          EnqueuePacket(pa, header);
          return false;
#else
          ucb (route,p,header);
          return true;
#endif
        }
    }
    /*
  NS_LOG_LOGIC ("Drop packet " << p->GetUid ()
                               << " as there is no route to forward it.");
    */
#ifndef DA
  NS_LOG_DEBUG("Route not found");
  
  Ptr<Ipv4Route> rt = Create<Ipv4Route> ();
  rt->SetDestination (dst);
  rt->SetSource (origin);
  rt->SetGateway (Ipv4Address ("127.0.0.1"));
  rt->SetOutputDevice (m_lo);
  
  EnqueueForNoDA(ucb, rt, p, header);
#endif
  return false;
}

void
RoutingProtocol::EnqueueForNoDA(UnicastForwardCallback ucb, Ptr<Ipv4Route> rt, Ptr<const Packet> p, const Ipv4Header &header)
{
  struct DeferredPack tmp;
  tmp.ucb = ucb;
  tmp.rt = rt;
  tmp.p = p;
  tmp.header = header;
  DeferredQueue.push_back(tmp);
  
  Simulator::Schedule (MilliSeconds (100),&RoutingProtocol::AutoDequeueNoDA,this);
}
  
void
RoutingProtocol::AutoDequeueNoDA()
{
  while(DeferredQueue.size())
    {
      struct DeferredPack tmp = DeferredQueue.front();
      tmp.ucb(tmp.rt, tmp.p, tmp.header);
      DeferredQueue.erase(DeferredQueue.begin());
    }
}
  
void
RoutingProtocol::RecvLeach (Ptr<Socket> socket)
{
  Address sourceAddress;
  Ptr<Packet> packet = socket->RecvFrom (sourceAddress);
  InetSocketAddress inetSourceAddr = InetSocketAddress::ConvertFrom (sourceAddress);
  Ipv4Address sender = inetSourceAddr.GetIpv4 ();
  Ipv4Address receiver = m_socketAddresses[socket].GetLocal ();
  double dist, dx, dy;
  LeachHeader leachHeader;
  Vector senderPosition;
  
  // maintain list of received advertisements
  // always choose the closest CH to join in
  // if itself is CH, pass this phase
  packet->RemoveHeader(leachHeader);
  
  /*
  NS_LOG_DEBUG(leachHeader.GetAddress());
  NS_LOG_DEBUG(isSink);
  NS_LOG_DEBUG(m_mainAddress);
  */
  
  if(isSink) return;
  if(leachHeader.GetAddress() == Ipv4Address("255.255.255.255")) {
      NS_LOG_DEBUG("Recv broadcast from CH: " << sender);
    // Need to update a new route
    RoutingTableEntry newEntry (
      /*device=*/ socket->GetBoundNetDevice(), /*dst (sink)*/m_sinkAddress,
      /*iface=*/ m_ipv4->GetAddress (m_ipv4->GetInterfaceForAddress (receiver), 0),
      /*next hop=*/ sender);
    
    senderPosition = leachHeader.GetPosition();
    dx = senderPosition.x - m_position.x;
    dy = senderPosition.y - m_position.y;
    dist = dx*dx + dy*dy;
    NS_LOG_DEBUG("dist = " << dist << ", m_dist = " << m_dist);
    
    if(dist < m_dist) {
      m_dist = dist;
      m_targetAddress = sender;
      m_bestRoute = newEntry;
      NS_LOG_DEBUG(sender);
    }
  }else {
    // Record cluster member
    m_clusterMember.push_back(leachHeader.GetAddress());
  }
}

void
RoutingProtocol::RespondToClusterHead()
{
  Ptr<Socket> socket = FindSocketWithAddress(m_mainAddress);
  Ptr<Packet> packet = Create<Packet> ();
  LeachHeader leachHeader;
  Ipv4Address ipv4;
  OutputStreamWrapper temp = OutputStreamWrapper(&std::cout);

  
  // Add routing to routingTable
  if(m_targetAddress != ipv4) {
    RoutingTableEntry newEntry, entry2;
    newEntry.Copy(m_bestRoute);
    entry2.Copy(m_bestRoute);
    Ptr<Ipv4Route> newRoute = newEntry.GetRoute();
    newRoute->SetDestination(m_targetAddress);
    newEntry.SetRoute(newRoute);

    if(m_bestRoute.GetInterface().GetLocal() != ipv4) m_routingTable.AddRoute (entry2);
    if(newEntry.GetInterface().GetLocal() != ipv4) m_routingTable.AddRoute (newEntry);

//    m_routingTable.Print(&temp);
      
    leachHeader.SetAddress(m_mainAddress);
    packet->AddHeader (leachHeader);
    socket->SendTo (packet, 0, InetSocketAddress (m_targetAddress, LEACH_PORT));
  }
}

void
RoutingProtocol::SendBroadcast ()
{
  Ptr<Socket> socket = FindSocketWithAddress (m_mainAddress);
  Ptr<Packet> packet = Create<Packet> ();
  LeachHeader leachHeader;
  Ipv4Address destination = Ipv4Address ("10.1.1.255");;

  socket->SetAllowBroadcast (true);

  leachHeader.SetPosition (m_position);
  packet->AddHeader (leachHeader);
  socket->SendTo (packet, 0, InetSocketAddress (destination, LEACH_PORT));
  
  RoutingTableEntry newEntry (
      /*device=*/ socket->GetBoundNetDevice(), /*dst (sink)*/m_sinkAddress,
      /*iface=*/ m_ipv4->GetAddress (m_ipv4->GetInterfaceForAddress (m_mainAddress), 0),
      /*next hop=*/ m_sinkAddress);
  m_routingTable.AddRoute (newEntry);
}
  
void
RoutingProtocol::PeriodicUpdate ()
{
  double prob = m_uniformRandomVariable->GetValue (0,1);
  // 10 round a cycle, 100/10=10 cluster heads per round
  int n = 10;
  double p = 1.0/n;
  double t = p/(1-p*(Round%n));
  
  NS_LOG_DEBUG("PeriodicUpdate!!");
//  NS_LOG_DEBUG("prob = " << prob << ", t = " << t);

  m_routingTable.DeleteRoute(m_targetAddress);
  m_routingTable.DeleteRoute(m_sinkAddress);
/*
  OutputStreamWrapper temp = OutputStreamWrapper(&std::cout);
  m_routingTable.Print(&temp);
*/
  if(Round%n == 0) valid = 1;
  Round++;
  m_dist = 1e100;
  cluster_head_this_round = 0;
  m_clusterMember.clear();
  m_bestRoute.Reset();
  m_targetAddress = Ipv4Address();
  
  if(prob < t && valid) {
    // become cluster head
    // broadcast info
    NS_LOG_DEBUG(m_mainAddress << " becomes cluster head");
    valid = 0;
    cluster_head_this_round = 1;
    m_targetAddress = m_sinkAddress;
    m_broadcastClusterHeadTimer.Schedule (MicroSeconds (m_uniformRandomVariable->GetInteger (10000,50000)));
  }else {
    m_respondToClusterHeadTimer.Schedule (MilliSeconds(100) + MicroSeconds (m_uniformRandomVariable->GetInteger (0,1000)));
  }
  m_periodicUpdateTimer.Schedule (m_periodicUpdateInterval + MicroSeconds (m_uniformRandomVariable->GetInteger (0,1000)));
}

void
RoutingProtocol::SetIpv4 (Ptr<Ipv4> ipv4)
{
  NS_ASSERT (ipv4 != 0);
  NS_ASSERT (m_ipv4 == 0);
  m_ipv4 = ipv4;
  // Create lo route. It is asserted that the only one interface up for now is loopback
  NS_ASSERT (m_ipv4->GetNInterfaces () == 1 && m_ipv4->GetAddress (0, 0).GetLocal () == Ipv4Address ("127.0.0.1"));
  m_lo = m_ipv4->GetNetDevice (0);
  NS_ASSERT (m_lo != 0);
  // Remember lo route
  RoutingTableEntry rt (
    /*device=*/ m_lo,  /*dst=*/
    Ipv4Address::GetLoopback (),
    /*iface=*/ Ipv4InterfaceAddress (Ipv4Address::GetLoopback (),Ipv4Mask ("255.0.0.0")),
    /*next hop=*/ Ipv4Address::GetLoopback ());
  rt.SetFlag (INVALID);
  m_routingTable.AddRoute (rt);
  Simulator::ScheduleNow (&RoutingProtocol::Start,this);
}

void
RoutingProtocol::NotifyInterfaceUp (uint32_t i)
{
  NS_LOG_FUNCTION (this << m_ipv4->GetAddress (i, 0).GetLocal ()
                        << " interface is up");
  Ptr<Ipv4L3Protocol> l3 = m_ipv4->GetObject<Ipv4L3Protocol> ();
  Ipv4InterfaceAddress iface = l3->GetAddress (i,0);
  if (iface.GetLocal () == Ipv4Address ("127.0.0.1"))
    {
      return;
    }
  // Create a socket to listen only on this interface
  Ptr<Socket> socket = Socket::CreateSocket (GetObject<Node> (),UdpSocketFactory::GetTypeId ());
  NS_ASSERT (socket != 0);
  socket->SetRecvCallback (MakeCallback (&RoutingProtocol::RecvLeach,this));
  socket->Bind (InetSocketAddress (Ipv4Address::GetAny (), LEACH_PORT));
  socket->BindToNetDevice (l3->GetNetDevice (i));
  socket->SetAllowBroadcast (true);
  socket->SetAttribute ("IpTtl",UintegerValue (1));
  m_socketAddresses.insert (std::make_pair (socket,iface));
  // Add local broadcast record to the routing table
  Ptr<NetDevice> dev = m_ipv4->GetNetDevice (m_ipv4->GetInterfaceForAddress (iface.GetLocal ()));
  RoutingTableEntry rt (/*device=*/ dev, /*dst=*/ iface.GetBroadcast (),/*iface=*/ iface, /*next hop=*/ iface.GetBroadcast ());
  m_routingTable.AddRoute (rt);
  if (m_mainAddress == Ipv4Address ())
    {
      m_mainAddress = iface.GetLocal ();
    }
  NS_ASSERT (m_mainAddress != Ipv4Address ());
}

void
RoutingProtocol::NotifyInterfaceDown (uint32_t i)
{
  Ptr<Ipv4L3Protocol> l3 = m_ipv4->GetObject<Ipv4L3Protocol> ();
  Ptr<NetDevice> dev = l3->GetNetDevice (i);
  Ptr<Socket> socket = FindSocketWithInterfaceAddress (m_ipv4->GetAddress (i,0));
  NS_ASSERT (socket);
  socket->Close ();
  m_socketAddresses.erase (socket);
  if (m_socketAddresses.empty ())
    {
      NS_LOG_LOGIC ("No leach interfaces");
      m_routingTable.Clear ();
      return;
    }
  m_routingTable.DeleteAllRoutesFromInterface (m_ipv4->GetAddress (i,0));
}

void
RoutingProtocol::NotifyAddAddress (uint32_t i,
                                   Ipv4InterfaceAddress address)
{
  NS_LOG_FUNCTION (this << " interface " << i << " address " << address);
  Ptr<Ipv4L3Protocol> l3 = m_ipv4->GetObject<Ipv4L3Protocol> ();
  if (!l3->IsUp (i))
    {
      return;
    }
  Ipv4InterfaceAddress iface = l3->GetAddress (i,0);
  Ptr<Socket> socket = FindSocketWithInterfaceAddress (iface);
  if (!socket)
    {
      if (iface.GetLocal () == Ipv4Address ("127.0.0.1"))
        {
          return;
        }
      Ptr<Socket> socket = Socket::CreateSocket (GetObject<Node> (),UdpSocketFactory::GetTypeId ());
      NS_ASSERT (socket != 0);
      socket->SetRecvCallback (MakeCallback (&RoutingProtocol::RecvLeach,this));
      // Bind to any IP address so that broadcasts can be received
      socket->Bind (InetSocketAddress (Ipv4Address::GetAny (), LEACH_PORT));
      socket->BindToNetDevice (l3->GetNetDevice (i));
      socket->SetAllowBroadcast (true);
      m_socketAddresses.insert (std::make_pair (socket,iface));
      Ptr<NetDevice> dev = m_ipv4->GetNetDevice (m_ipv4->GetInterfaceForAddress (iface.GetLocal ()));
      RoutingTableEntry rt (/*device=*/ dev, /*dst=*/ iface.GetBroadcast (), /*iface=*/ iface, /*next hop=*/ iface.GetBroadcast ());
      m_routingTable.AddRoute (rt);
    }
}

void
RoutingProtocol::NotifyRemoveAddress (uint32_t i,
                                      Ipv4InterfaceAddress address)
{
  Ptr<Socket> socket = FindSocketWithInterfaceAddress (address);
  if (socket)
    {
      m_socketAddresses.erase (socket);
      Ptr<Ipv4L3Protocol> l3 = m_ipv4->GetObject<Ipv4L3Protocol> ();
      if (l3->GetNAddresses (i))
        {
          Ipv4InterfaceAddress iface = l3->GetAddress (i,0);
          // Create a socket to listen only on this interface
          Ptr<Socket> socket = Socket::CreateSocket (GetObject<Node> (),UdpSocketFactory::GetTypeId ());
          NS_ASSERT (socket != 0);
          socket->SetRecvCallback (MakeCallback (&RoutingProtocol::RecvLeach,this));
          // Bind to any IP address so that broadcasts can be received
          socket->Bind (InetSocketAddress (Ipv4Address::GetAny (), LEACH_PORT));
          socket->SetAllowBroadcast (true);
          m_socketAddresses.insert (std::make_pair (socket,iface));
        }
    }
}

Ptr<Socket>
RoutingProtocol::FindSocketWithAddress (Ipv4Address addr) const
{
  for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::const_iterator j = m_socketAddresses.begin (); j
       != m_socketAddresses.end (); ++j)
    {
      Ptr<Socket> socket = j->first;
      Ipv4Address iface = j->second.GetLocal();
      if (iface == addr)
        {
          return socket;
        }
    }
  return NULL;
}

Ptr<Socket>
RoutingProtocol::FindSocketWithInterfaceAddress (Ipv4InterfaceAddress addr) const
{
  for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::const_iterator j = m_socketAddresses.begin (); j
       != m_socketAddresses.end (); ++j)
    {
      Ptr<Socket> socket = j->first;
      Ipv4InterfaceAddress iface = j->second;
      if (iface == addr)
        {
          return socket;
        }
    }
  return NULL;
}

void
RoutingProtocol::Send (Ptr<Ipv4Route> route,
                       Ptr<const Packet> packet,
                       const Ipv4Header & header)
{
  Ptr<Ipv4L3Protocol> l3 = m_ipv4->GetObject<Ipv4L3Protocol> ();
  NS_ASSERT (l3 != 0);
  Ptr<Packet> p = packet->Copy ();
  l3->Send (p,route->GetSource (),header.GetDestination (),header.GetProtocol (),route);
}

void
RoutingProtocol::Drop (Ptr<const Packet> packet,
                       const Ipv4Header & header,
                       Socket::SocketErrno err)
{
  NS_LOG_DEBUG (m_mainAddress << " drop packet " << packet->GetUid () << " to "
                              << header.GetDestination () << " from queue. Error " << err);
}

void
RoutingProtocol::EnqueuePacket (Ptr<Packet> p,
                                const Ipv4Header & header)
{
  NS_LOG_FUNCTION (this << ", " << p << ", " << header);
  NS_ASSERT (p != 0 && p != Ptr<Packet> ());
  
  Ptr<Packet> out;
  UdpHeader uhdr;
  LeachHeader leachHeader;
  uint32_t slot = p->GetUid()%1021;
  struct hash* now = m_hash[slot];
  
  NS_LOG_DEBUG("IsDontFragement: " << header.IsDontFragment());
  
  if(header.GetFragmentOffset() == 0) p->RemoveHeader(uhdr);

  while (now != NULL)
    {
      if(now->uid == p->GetUid())
        break;
      now = now->next;
    }
  if(now != NULL)
    {
      NS_LOG_DEBUG("now->p size " << now->p->GetSize() << ", p size " << p->GetSize());
      now->p->AddAtEnd(p);
      p = now->p;
      NS_LOG_DEBUG("after p size " << p->GetSize());
    }
  
  while(DeAggregate(p, out, leachHeader))
    {
      QueueEntry newEntry (out,header);
      bool result = m_queue.Enqueue (newEntry);
      struct msmt temp;
      
      temp.begin = Simulator::Now();
      temp.end = leachHeader.GetDeadline();
      timeline.push_back(temp);
      if (result)
        {
          NS_LOG_DEBUG ("Added packet " << out->GetUid () << " to queue.");
        }
    }
}

bool
RoutingProtocol::DeAggregate (Ptr<Packet> in, Ptr<Packet>& out, LeachHeader& lhdr)
{
  if(in->GetSize() >= 56)
    {
      LeachHeader leachHeader;
      in->RemoveHeader(leachHeader);
      in->RemoveAtStart(16);
      
      lhdr = leachHeader;
      out = new Packet(16);
      out->AddHeader(leachHeader);
      NS_LOG_DEBUG("deadline" << leachHeader.GetDeadline());
      return true;
    }
  uint32_t slot = in->GetUid()%1021;
  struct hash* now = m_hash[slot];
  while(now != NULL)
    {
      if(now->uid == in->GetUid()) break;
      now = now->next;
    }
  if(now == NULL) 
    {
      now = new struct hash;
      now->uid = in->GetUid();
      now->next = m_hash[slot];
      m_hash[slot] = now;
    }
  now->p = in;
  NS_LOG_DEBUG("Size left " << in->GetSize() << ", on UID " << in->GetUid());
  
  return false;
}

bool
RoutingProtocol::DataAggregation (Ptr<Packet> p)
{
  // Implement data aggregation policy
  // and data addgregation function

#ifdef DA_PROP
  return Proposal(p);
#endif
#ifdef DA_OPT
  return OptTM(p);
#endif
#ifdef DA_CL
  return ControlLimit(p);
#endif
#ifdef DA_SF
  return SelectiveForwarding(p);
#endif
  
  return true;
}
  
bool
RoutingProtocol::Proposal (Ptr<Packet> p)
{
  NS_LOG_FUNCTION (this);
  // pick up those selected entry and send
  int expired = 0, expected;
  //LeachHeader hdr;
  Time deadLine = Now();
  
  // 1.28 = 2*0.64, 0.064 = 64bytes/8kbps
  // average 10 cluster heads
  // average 10 members per cluster
  deadLine += Seconds(m_queue.GetSize()/m_lambda);
  if(!cluster_head_this_round)
    // depend on average tx size from cluster member
    // depend on deadline setting
    // * average packet_size?
    deadLine += Seconds(0.064+1.0/m_lambda);

//  NS_LOG_UNCOND("Now: " << Now() << ", Deadline: " << deadLine);
  for (int i=0; i<(int)m_queue.GetSize(); i++)
    {
      NS_LOG_DEBUG("GetDeadline: " << m_queue[i].GetDeadline() << ", UID: " << m_queue[i].GetPacket()->GetUid() << ", Now: " << Now());
      if(m_queue[i].GetDeadline() < Now())
        {
          // drop it
          NS_LOG_DEBUG("Drop");
//          NS_LOG_DEBUG("GetLeachHeader: " << m_queue[i].GetDeadline() << ", Now: " << Now());
          m_queue.Drop (i);
          m_dropped++;
          i--;
        }
    }
  
  for(uint32_t i=0; i<m_queue.GetSize(); i++) {
//    NS_LOG_UNCOND("GetDeadline: " << m_queue[i].GetDeadline() << ", deadline: " << deadLine);
    if(m_queue[i].GetDeadline() < deadLine) {
      expired++;
    }
  }
  if(cluster_head_this_round) {
    expected = 1+m_clusterMember.size();
  }
  else {
    expected = 1;
  }
  
//  NS_LOG_UNCOND("expired: " << expired << ", expected: " << expected);
  if(expired >= expected || Now() > Seconds(48.5)) {
    // merge data
    QueueEntry temp;
    
    while(m_queue.Dequeue(m_sinkAddress, temp)) {
      p->AddAtEnd(temp.GetPacket());
    }
    
    return true;
  }
  return false;
}

bool
RoutingProtocol::OptTM (Ptr<Packet> p)
{
  Time time = Now();
  uint32_t rewards[100], maxR = 0;
  uint32_t actions[100];
  static int step = 0;
  
  for(int i=0; i<100; i++)
    {
      actions[i] = 0;
      rewards[i] = 0;
      for(uint j=0; j<m_queue.GetSize(); j++)
        {
          if(m_queue[j].GetDeadline() >= time) rewards[i] += m_queue[j].GetDeadline().ToInteger(Time::MS) - time.ToInteger(Time::MS);
        }
      for(int j=1; j<i+step; j++)
        {
          rewards[i] += (j<8) ?30000-j*4000 :0;
        }
      time += Seconds(1/m_lambda);
    }
  
  for(int i=0; i<100; i++)
    {
      if(rewards[i] > maxR)
        {
          maxR = rewards[i];
        }
    }
  
  // wait=1, transmit=2
  for(int i=98; i>=0; i--)
    {
      if(rewards[i] < maxR)
        actions[i] = 1;
      else
        {
          double rb[100], rn[100];
          rb[99] = 1.0;
          rn[99] = 0.0;

          for(int k=98; k>i; k--)
            {
              rn[k] = max(0.0, i*rn[k+1]/(k+1) + rb[k+1]/(k+1));
              rb[k] = max(rewards[k], rn[k]);
            }

          if(rewards[i] >= (uint32_t)rb[i+1])
            actions[i] = 2;
          else
            actions[i] = 1;
        }
    }
  step++;
  
  if(actions[0] > 1 || Now() > Seconds(48.5))
    {
      QueueEntry temp;
      while(m_queue.Dequeue(m_sinkAddress, temp))
        {
          p->AddAtEnd(temp.GetPacket());
        }
      
      return true;
    }
  
  return false;
}
  
bool
RoutingProtocol::ControlLimit (Ptr<Packet> p)
{
  static uint32_t threshold = (1/(log(1/0.1)*(log(1/0.1)+m_lambda)))+2;
  for(int i=0; i<(int)m_queue.GetSize(); i++)
    {
      if(m_queue[i].GetDeadline() < Now())
        {
          m_queue.Drop(i);
          m_dropped++;
          i--;
        }
    }
    
  if(m_queue.GetSize() >= threshold || Now() > Seconds(48.5))
    {
      QueueEntry temp;
      while(m_queue.Dequeue(m_sinkAddress, temp)) {
        p->AddAtEnd(temp.GetPacket());
      }
      return true;
    }
  return false;
}
  
bool
RoutingProtocol::SelectiveForwarding (Ptr<Packet> p)
{
  return false;
}

}
}
