
#include "sip2/util/ParseBuffer.hxx"
#include "sip2/util/DataStream.hxx"
#include "sip2/sipstack/Symbols.hxx"
#include "sip2/sipstack/Uri.hxx"
#include "sip2/sipstack/UnknownParameter.hxx"
#include "sip2/sipstack/ParserCategories.hxx" // !dlb! just NameAddr
#include "sip2/sipstack/SipMessage.hxx"
#include "sip2/sipstack/Embedded.hxx"
#include "sip2/util/Logger.hxx"

using namespace Vocal2;

#define VOCAL_SUBSYSTEM Subsystem::SIP

Uri::Uri() 
   : ParserCategory(),
     mScheme(Symbols::DefaultSipScheme),
     mPort(0),
     mOldPort(0),
     mEmbeddedHeaders(0)
{
}

Uri::Uri(const Data& data)
   : ParserCategory(), 
     mScheme(Symbols::DefaultSipScheme),
     mPort(0),
     mOldPort(0),
     mEmbeddedHeaders(0)
{
   ParseBuffer pb(data.data(), data.size());
   Uri tmp;

   try
   {
      tmp.parse(pb);
   }
   catch ( std::exception* e )
   {
      mScheme = Symbols::DefaultSipScheme;
      mHost = Data::Empty;
      mUser = Data::Empty;
      mPort = -1;

      throw e;
   }

   *this = tmp;
}

Uri::Uri(const Uri& rhs)
   : ParserCategory(rhs),
     mScheme(rhs.mScheme),
     mHost(rhs.mHost),
     mUser(rhs.mUser),
     mPort(rhs.mPort),
     mPassword(rhs.mPassword),
     mOldPort(0),
     mEmbeddedHeadersText(rhs.mEmbeddedHeadersText),
     mEmbeddedHeaders(rhs.mEmbeddedHeaders ? new SipMessage(*rhs.mEmbeddedHeaders) : 0)
{}

Uri::~Uri()
{
   delete mEmbeddedHeaders;
}

bool
Uri::hasEmbedded() const
{
   checkParsed(); 
   return !mEmbeddedHeadersText.empty() || mEmbeddedHeaders != 0;
}

Uri&
Uri::operator=(const Uri& rhs)
{
   if (this != &rhs)
   {
      ParserCategory::operator=(rhs);
      mScheme = rhs.mScheme;
      mHost = rhs.mHost;
      mUser = rhs.mUser;
      mPort = rhs.mPort;
      mPassword = rhs.mPassword;
      if (rhs.mEmbeddedHeaders != 0)
      {
         delete mEmbeddedHeaders;
         mEmbeddedHeaders = new SipMessage(*rhs.mEmbeddedHeaders);
      }
      else
      {
         mEmbeddedHeadersText = rhs.mEmbeddedHeadersText;
      }
   }
   return *this;
}

class OrderUnknownParameters
{
   public:
      OrderUnknownParameters() { notUsed=false; };
      ~OrderUnknownParameters() {};

      bool operator()(const Parameter* p1, const Parameter* p2) const
      {
         return dynamic_cast<const UnknownParameter*>(p1)->getName() < dynamic_cast<const UnknownParameter*>(p2)->getName();
      }
   private:
      bool notUsed;
};

bool 
Uri::operator==(const Uri& other) const
{
   if (isEqualNoCase(mScheme, other.mScheme) &&
       isEqualNoCase(mHost, other.mHost) &&
       mUser == other.mUser &&
       mPassword == other.mPassword &&
       mPort == other.mPort)
   {
      for (ParameterList::iterator it = mParameters.begin(); it != mParameters.end(); it++)
      {
         Parameter* otherParam = other.getParameterByEnum((*it)->getType());
         switch ((*it)->getType())
         {
            case ParameterTypes::user:
            {
               if (!(otherParam &&
                     isEqualNoCase(dynamic_cast<DataParameter*>(*it)->value(),
                                   dynamic_cast<DataParameter*>(otherParam)->value())))
               {
                  return false;
               }
            }
            break;
            case ParameterTypes::ttl:
            {
               if (!(otherParam &&
                     dynamic_cast<IntegerParameter*>(*it)->value(),
                     dynamic_cast<IntegerParameter*>(otherParam)->value()))
               {
                  return false;
               }
            }
            case ParameterTypes::method:
            {
               // this should possilby be case sensitive, but is allowed to be
               // case insensitive for robustness.  
               if (!(otherParam &&
                     isEqualNoCase(dynamic_cast<DataParameter*>(*it)->value(),
                                   dynamic_cast<DataParameter*>(otherParam)->value())))
               {
                  return false;
               }
            }
            break;
            case ParameterTypes::maddr:
            {               
               if (!(otherParam &&
                     isEqualNoCase(dynamic_cast<DataParameter*>(*it)->value(),
                                   dynamic_cast<DataParameter*>(otherParam)->value())))
               {
                  return false;
               }
            }
            break;
            case ParameterTypes::transport:
            {
               if (!(otherParam &&
                     isEqualNoCase(dynamic_cast<DataParameter*>(*it)->value(),
                                   dynamic_cast<DataParameter*>(otherParam)->value())))
               {
                  return false;
               }
            }
            break;
            // the parameters that follow don't affect comparison if only present
            // in one of the URI's
            case ParameterTypes::lr:
               break;
            default:
               break;
               //treat as unknown parameter?
         }
      }         
   }
   else
   {
      return false;
   }
   ParameterList unA = mUnknownParameters;
   ParameterList unB = other.mUnknownParameters;

   OrderUnknownParameters orderUnknown;

   unA.sort(orderUnknown);  
   unB.sort(orderUnknown);
 
   ParameterList::iterator a = unA.begin();
   ParameterList::iterator b = unB.begin();

   while(a != unA.end() && b != unB.end())
   {
      if (orderUnknown(*a, *b))
      {
         a++;
      }
      else if (orderUnknown(*b, *a))
      {
         b++;
      }
      else
      {
         if (!isEqualNoCase(dynamic_cast<UnknownParameter*>(*a)->value(),
                            dynamic_cast<UnknownParameter*>(*b)->value()))
         {
            return false;
         }
         a++;
         b++;
      }
   }
   return true;
}

bool 
Uri::operator!=(const Uri& other) const
{
   return !(*this == other);
}

bool
Uri::operator<(const Uri& other) const
{
   if (mUser < other.mUser)
   {
      return true;
   }

   if (mUser > other.mUser)
   {
      return false;
   }

   if (mHost < other.mHost)
   {
      return true;
   }

   if (mHost > other.mHost)
   {
      return false;
   }

   return mPort < other.mPort;
}

const Data
Uri::getAorNoPort() const
{
   checkParsed();
       
   Data aor( (mUser.empty()) ? (mHost) : (mUser + Symbols::AT_SIGN + mHost) );
   
   return aor;
}

const Data&
Uri::getAor() const
{
   checkParsed();
   // did anything change?
   if (mOldUser != mUser ||
       mOldHost != mHost ||
       mOldPort != mPort)
   {
      mOldHost = mHost;
      mOldUser = mUser;
      mOldPort = mPort;

#if defined(VOCAL2_AOR_HAS_SCHEME) // !jf! lame
      mAor = mScheme + Symbols::COLON;
#else
      mAor.clear();
#endif
      
      // mAor.reserve(mUser.size() + mHost.size() + 10); !dlb!
      if (mUser.empty())
      {
         mAor += mHost;
      }
      else
      {
         mAor += mUser;
         mAor += Symbols::AT_SIGN;
         mAor += mHost;
      }

      if (mPort != 0)
      {
         mAor += Symbols::COLON;
         mAor += Data(mPort);
      }
#if defined(VOCAL2_AOR_HAS_SCHEME)
      else
      {
         mAor += Data(Symbols::COLON) + Data(5060);
      }
#endif
   }
   return mAor;
}

void
Uri::parse(ParseBuffer& pb)
{
   pb.skipWhitespace();
   const char* start = pb.position();
   pb.skipToChar(Symbols::COLON[0]);

   if ( !pb.eof() )
   {
      pb.data(mScheme, start);
      pb.skipChar();   
   }
   else
   {
      pb.reset(start);
   }

   if (!(isEqualNoCase(mScheme, Symbols::Sip) || isEqualNoCase(mScheme, Symbols::Sips)))
   {
      start = pb.position();
      pb.skipToEnd();
      assert( start <= pb.position() );
      pb.data(mHost, start);
      return;
   }
   
   mScheme.lowercase();
   start = pb.position();
   pb.skipToChar(Symbols::AT_SIGN[0]);
   if (!pb.eof())
   {
      pb.reset(start);
      start = pb.position();
      pb.skipToOneOf(":@");
      pb.data(mUser, start);
      if (*pb.position() == Symbols::COLON[0])
      {
         start = pb.skipChar();
         pb.skipToChar(Symbols::AT_SIGN[0]);
         pb.data(mPassword, start);
      }
      start = pb.skipChar();
   }
   else
   {
      pb.reset(start);
   }
   
   if (*start == '[')
   {
      pb.skipToChar(']');
   }
   else
   {
      pb.skipToOneOf(ParseBuffer::Whitespace, ":;?>");
   }
   pb.data(mHost, start);

   pb.skipToOneOf(ParseBuffer::Whitespace, ":;?>");
   if (!pb.eof() && *pb.position() == ':')
   {
      start = pb.skipChar();
      mPort = pb.integer();
      pb.skipToOneOf(ParseBuffer::Whitespace, ";?>");
   }
   else
   {
      mPort = 0;
   }
   
   parseParameters(pb);

   if (*pb.position() == Symbols::QUESTION[0])
   {
      const char* anchor = pb.position();
      pb.skipToOneOf(Symbols::RA_QUOTE, Symbols::SEMI_COLON);
      pb.data(mEmbeddedHeadersText, anchor);
   }
}

ParserCategory*
Uri::clone() const
{
   return new Uri(*this);
}
 
std::ostream& 
Uri::encodeParsed(std::ostream& str) const
{
   str << mScheme << Symbols::COLON; 
   if (!mUser.empty())
   {
      str << mUser;
      if (!mPassword.empty())
      {
         str << Symbols::COLON << mPassword;
      }
      str << Symbols::AT_SIGN;
   }
   str << mHost;
   if (mPort != 0)
   {
      str << Symbols::COLON << mPort;
   }
   encodeParameters(str);
   encodeEmbeddedHeaders(str);

   return str;
}

SipMessage&
Uri::embedded() const
{
   checkParsed();
   if (mEmbeddedHeaders == 0)
   {
      Uri* ncthis = const_cast<Uri*>(this);
      ncthis->mEmbeddedHeaders = new SipMessage();
      if (!mEmbeddedHeadersText.empty())
      {
         ParseBuffer pb(mEmbeddedHeadersText.data(), mEmbeddedHeadersText.size());
         ncthis->parseEmbeddedHeaders(pb);
      }
   }

   return *mEmbeddedHeaders;
}

void
Uri::parseEmbeddedHeaders(ParseBuffer& pb)
{
   DebugLog(<< "Uri::parseEmbeddedHeaders");
   if (*pb.position() == Symbols::QUESTION[0])
   {
      pb.skipChar();
   }

   const char* anchor;
   Data headerName;
   Data headerContents;

   bool first = true;
   while (!pb.eof())
   {
      if (first)
      {
         first = false;
      }
      else
      {
         pb.skipChar(Symbols::AMPERSAND[0]);
      }

      anchor = pb.position();
      pb.skipToChar(Symbols::EQUALS[0]);
      pb.data(headerName, anchor);
      // .dlb. in theory, need to decode header name

      anchor = pb.skipChar(Symbols::EQUALS[0]);
      pb.skipToChar(Symbols::AMPERSAND[0]);
      pb.data(headerContents, anchor);

      unsigned int len;
      char* decodedContents = Embedded::decode(headerContents, len);
      mEmbeddedHeaders->addBuffer(decodedContents);

      static const Data body("Body");
      if (isEqualNoCase(body, headerName))
      {
         mEmbeddedHeaders->setBody(decodedContents, len); 
      }
      else
      {
         DebugLog(<< "Uri::parseEmbeddedHeaders(" << headerName << ", " << Data(decodedContents, len) << ")");
         mEmbeddedHeaders->addHeader(Headers::getType(headerName.data(), headerName.size()),
                                     headerName.data(), headerName.size(),
                                     decodedContents, len);
      }
   }
}

std::ostream& 
Uri::encodeEmbeddedHeaders(std::ostream& str) const
{
   if (mEmbeddedHeaders)
   {
      mEmbeddedHeaders->encodeEmbedded(str);
   }
   else
   {
      // never decoded
      str << mEmbeddedHeadersText;
   }
   return str;
}

/* ====================================================================
 * The Vovida Software License, Version 1.0 
 * 
 * Copyright (c) 2000 Vovida Networks, Inc.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 
 * 3. The names "VOCAL", "Vovida Open Communication Application Library",
 *    and "Vovida Open Communication Application Library (VOCAL)" must
 *    not be used to endorse or promote products derived from this
 *    software without prior written permission. For written
 *    permission, please contact vocal@vovida.org.
 *
 * 4. Products derived from this software may not be called "VOCAL", nor
 *    may "VOCAL" appear in their name, without prior written
 *    permission of Vovida Networks, Inc.
 * 
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, TITLE AND
 * NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL VOVIDA
 * NETWORKS, INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT DAMAGES
 * IN EXCESS OF $1,000, NOR FOR ANY INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * 
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by Vovida
 * Networks, Inc. and many individuals on behalf of Vovida Networks,
 * Inc.  For more information on Vovida Networks, Inc., please see
 * <http://www.vovida.org/>.
 *
 */
