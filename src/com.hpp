// Copyright (C) 2015 Jonas Kümmerlin <rgcjonas@gmail.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#pragma once

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif

#include <unknwn.h>

#include <type_traits>
#include <utility>
#include <algorithm>

namespace com {
    template<typename TInterface> class ptr;

    template<typename TInterface> TInterface **out_arg(ptr<TInterface> &ptr);
    template<typename TInterface> TInterface * const *single_item_array(ptr<TInterface> &ptr);
    template<typename T> ptr<T> take_ptr(T *raw);
    template<typename T> ptr<T> ref_ptr(T *ptr);

    /**
     * This is designed to feel like a real, raw pointer to a com interface
     * but tries to ensure memory safety. The interface is modeled after
     * std::shared_ptr, but lighter and with extra convenience.
     */
    template<typename TInterface>
    class ptr
    {
        TInterface *p { nullptr };

#ifndef CINTERFACE
        static_assert(std::is_base_of<IUnknown, TInterface>::value, "The COM interface must inherit from IUnknown!");
#endif

        inline void ref()
        {
            if (!p)
                return;

#ifdef CINTERFACE
            p->lpVtbl->AddRef(p);
#else
            p->AddRef();
#endif
        }

        // You should usually prefer reset()
        inline void unref()
        {
            if (!p)
                return;

#ifdef CINTERFACE
            p->lpVtbl->Release(p);
#else
            p->Release();
#endif
        }

    public:
        inline void reset()
        {
            unref();
            p = nullptr;
        }

        /**
         * Constructs an empty instance (containing a null pointer)
         *
         * Constructing from a raw pointer is not defined on purpose, because
         * a com::ptr instance casts itself to a raw pointer at the slightest provocation,
         * and stuffing this pointer into a new com_ptr instance by another implicit conversion
         * is paving the way for a double release and great debugging pain.
         *
         * Use com::ref_ptr and com::take_ptr to wrap a dumb pointer.
         */
        inline ptr() = default;

        inline ~ptr()
        {
            reset();
        }

        /**
         * Copy constructor: Copy the pointer and create a new reference
         */
        inline ptr(const ptr &other)
        {
            this->p = other.p;

            ref();
        }

        /**
         * Move constructor: Transfer the reference (clearing the old smart pointer)
         */
        inline ptr(ptr &&other)
        {
            std::swap(p, other.p);
        }

        /**
         * Upcasting: Create base class smart pointer from child smart pointer (copy)
         */
        template<class Tsub, class = typename std::enable_if<std::is_base_of<TInterface, Tsub>::value>::type>
        inline ptr(const ptr<Tsub> &other)
        {
            p = other.p;

            ref();
        }

        /**
         * Upcasting: Create base class smart pointer from child smart pointer (move)
         */
        template<class Tsub, class = typename std::enable_if<std::is_base_of<TInterface, Tsub>::value>::type>
        inline ptr(ptr<Tsub> &&other)
        {
            p = other.p;
            other.p = nullptr;
        }

        /**
         * Assignment
         */
        inline ptr& operator=(ptr<TInterface> other)
        {
            std::swap(p, other.p);

            return *this;
        }

        /**
         * Upcasting assignment
         */
        template<class Tsub, class = typename std::enable_if<std::is_base_of<TInterface, Tsub>::value>::type>
        inline ptr& operator=(ptr<Tsub> other)
        {
            p = other.p;
            other.p = nullptr;

            return *this;
        }

        /**
         * Check whether a valid pointer is stored in this smart pointer
         *
         * @returns true if there is a valid pointer, false if there is a null pointer
         */
        explicit operator bool() const
        {
            return p != nullptr;
        }

        /**
         * Silently convert to the wrapped pointer
         */
        operator TInterface*()
        {
            return p;
        }

        /**
         * Dereference operator, obtain a reference to the object
         *
         * This is not really useful, but present to behave like a good smart pointer
         */
        TInterface& operator*()
        {
            return *p;
        }

        TInterface* operator->()
        {
            return p;
        }

        TInterface *get()
        {
            return p;
        }

        TInterface *release()
        {
            TInterface *tmp = p;
            p = nullptr;
            return tmp;
        }

        /**
         * Retrieves a pointer to the supported interface T2 on the object wrapped by this com_ptr instance
         * as a new com_ptr smart pointer template.
         *
         * If the interface cannot be instanced, the returned com_ptr will carry a null pointer
         */
        template<typename T2> ptr<T2> query()
        {
            if (!p)
                return ptr<T2>();

            ptr<T2> smart;
#ifdef CINTERFACE
            p->lpVtbl->QueryInterface(p, smart.uuid(), reinterpret_cast<void**>(&smart.p));
#else
            p->QueryInterface(smart.uuid(), reinterpret_cast<void**>(&smart.p));
#endif

            return smart;
        }

        static REFIID uuid()
        {
            return __uuidof(TInterface);
        }

        // FRIENDS
        template<typename TOther> friend class ptr; // any other instance of this template
        template<typename T> friend T **out_arg(ptr<T> &ptr);
        template<typename T> friend T * const *single_item_array(ptr<T> &ptr);
        template<typename T> friend ptr<T> take_ptr(T *raw);
        template<typename T> friend ptr<T> ref_ptr(T *raw);
    };

    /**
     * Wrap a dumb pointer into a com::ptr, adopting the original reference
     */
    template<typename T>
    ptr<T> take_ptr(T *raw)
    {
        ptr<T> smart;
        smart.p = raw;

        return smart;
    }

    /**
     * Wrap a dumb pointer into a com::ptr, creating a new reference
     */
    template<typename T>
    ptr<T> ref_ptr(T* raw)
    {
        ptr<T> smart;
        smart.p = raw;

        return smart;
    }

    /**
     * Improves memory safety when dealing with com-style output arguments
     *
     * A caller is supposed to wrap output arguments with com::out_arg(com::ptr),
     * and the template hackery will make sure that the COM memory model is followed.
     *
     * Internally, it is not much more than a wrapper around a IInterface**
     */
    template<typename TInterface>
    TInterface **out_arg(ptr<TInterface> &smart)
    {
        smart.reset();
        return &smart.p;
    }

    /**
     * Only use this when you can't use IID_PPV_ARGS
     */
    template<typename TInterface>
    void **out_arg_void(ptr<TInterface> &smart)
    {
        return reinterpret_cast<void**>(out_arg(smart));
    }

    template<typename TInterface>
    TInterface * const *single_item_array(ptr<TInterface> &smart)
    {
        return &smart.p;
    }

    /**
     * Base class to inherit from when implementing com interfaces
     *
     * Rules:
     *  * Inherit from your interface and com::obj_impl_base
     *  * Do NOT implement IUnknown
     *  * If you inherit from multiple interfaces, redeclare the IUnknown methods.
     *    Should you neglect to do this, the com::ptr smart pointer will fail for your class.
     *  * Do implement _queryInterface()
     *  * Do not instantiate yourself, use com::make_object
     */
    class obj_impl_base
    {
    protected:
        /**
         * Returns a pointer to the interface specified by iid cast to void,
         * or, if the interface is not implemented on the given object,
         * a null pointer. The reference count is unaffected.
         *
         * IUnknown will not be requested and shall not be handled.
         *
         * Derived classes should normally implement this by calling
         * com::query_impl&lt;IIface1, Iface2, .. &gt;::on(this);
         *
         * This usually works, except when you've create a diamond of death inheritance.
         * Then you'll have to implement it (partly) yourself.
         */
        virtual void *_queryInterface(REFIID iid) = 0;
    };

    template<typename TImpl, typename... TArgs>
    ptr<TImpl> make_object(TArgs&&... args)
    {
        static_assert(std::is_base_of<com::obj_impl_base, TImpl>::value, "TImpl must inherit from com::object_impl_base");

        class controlling_unknown : public IUnknown
        {};

        class obj_impl final : public TImpl, public controlling_unknown
        {
            ULONG refcnt { 1 };

        public:
            using TImpl::TImpl;

            /*** IUnknown methods ***/
            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override
            {
                if (!ppvObject)
                    return E_INVALIDARG;

                *ppvObject = nullptr;

                if (riid == __uuidof(IUnknown)) {
                    *ppvObject = reinterpret_cast<void*>(static_cast<controlling_unknown *>(this));
                    this->AddRef();
                } else {
                    *ppvObject = this->_queryInterface(riid);
                    if (*ppvObject)
                        this->AddRef();
                }

                return E_NOINTERFACE;
            }

            ULONG STDMETHODCALLTYPE AddRef() override
            {
                return InterlockedIncrement(&refcnt);
            }

            ULONG STDMETHODCALLTYPE Release() override
            {
                ULONG ulRefCount = InterlockedDecrement(&refcnt);
                if (0 == ulRefCount)
                {
                    delete this;
                }
                return ulRefCount;
            }
        };

        return take_ptr<TImpl>(new obj_impl(std::forward<TArgs>(args)...));
    }

    template<typename... TIfaces>
    class query_impl
    {
        template<typename TObject, typename TIface1, typename... TOthers>
        static void *on_impl(TObject *o, REFIID iid)
        {
            if (iid == __uuidof(TIface1)) {
                return reinterpret_cast<void**>(static_cast<TIface1*>(o));
            } else {
                return on_impl<TObject, TOthers...>(o, iid);
            }
        }

        template<typename TObject>
        static void *on_impl(TObject *, REFIID)
        {
            return nullptr;
        }

        query_impl() = delete;

    public:
        template<typename TObject>
        static void *on(TObject *o, REFIID iid)
        {
            return on_impl<TObject, TIfaces...>(o, iid);
        }
    };
}; // namespace com