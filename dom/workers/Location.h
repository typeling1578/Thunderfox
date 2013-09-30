/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_workers_location_h__
#define mozilla_dom_workers_location_h__

#include "Workers.h"
#include "WorkerPrivate.h"
#include "nsWrapperCache.h"

BEGIN_WORKERS_NAMESPACE

class WorkerLocation MOZ_FINAL : public nsWrapperCache
{
  nsString mHref;
  nsString mProtocol;
  nsString mHost;
  nsString mHostname;
  nsString mPort;
  nsString mPathname;
  nsString mSearch;
  nsString mHash;

  WorkerLocation(const nsAString& aHref,
                 const nsAString& aProtocol,
                 const nsAString& aHost,
                 const nsAString& aHostname,
                 const nsAString& aPort,
                 const nsAString& aPathname,
                 const nsAString& aSearch,
                 const nsAString& aHash)
    : mHref(aHref)
    , mProtocol(aProtocol)
    , mHost(aHost)
    , mHostname(aHostname)
    , mPort(aPort)
    , mPathname(aPathname)
    , mSearch(aSearch)
    , mHash(aHash)
  {
    MOZ_COUNT_CTOR(WorkerLocation);
    SetIsDOMBinding();
  }

public:

  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(WorkerLocation)
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_NATIVE_CLASS(WorkerLocation)

  static already_AddRefed<WorkerLocation>
  Create(JSContext* aCx, JS::Handle<JSObject*> aGlobal,
         WorkerPrivate::LocationInfo& aInfo);

  virtual JSObject*
  WrapObject(JSContext* aCx, JS::Handle<JSObject*> aScope) MOZ_OVERRIDE;

  nsISupports* GetParentObject() const {
    return nullptr;
  }

  ~WorkerLocation()
  {
    MOZ_COUNT_DTOR(WorkerLocation);
  }

  void Stringify(nsString& aHref) const
  {
    aHref = mHref;
  }
  void GetHref(nsString& aHref) const
  {
    aHref = mHref;
  }
  void GetProtocol(nsString& aProtocol) const
  {
    aProtocol = mProtocol;
  }
  void GetHost(nsString& aHost) const
  {
    aHost = mHost;
  }
  void GetHostname(nsString& aHostname) const
  {
    aHostname = mHostname;
  }
  void GetPort(nsString& aPort) const
  {
    aPort = mPort;
  }
  void GetPathname(nsString& aPathname) const
  {
    aPathname = mPathname;
  }
  void GetSearch(nsString& aSearch) const
  {
    aSearch = mSearch;
  }
  void GetHash(nsString& aHash) const
  {
    aHash = mHash;
  }
};

END_WORKERS_NAMESPACE

#endif // mozilla_dom_workers_location_h__
