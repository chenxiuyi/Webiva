/* $Id: ruby_xml_xpath_context.c 799 2009-03-02 01:45:24Z cfis $ */

/* Please see the LICENSE file for copyright and distribution information */

#include "ruby_libxml.h"
#include "ruby_xml_xpath_context.h"
#include "ruby_xml_xpath_expression.h"

#if RUBY_ST_H
#include <ruby/st.h>
#else
#include <st.h>
#endif

/*
 * Document-class: LibXML::XML::XPath::Context
 *
 * The XML::XPath::Context class is used to evaluate XPath
 * expressions.  Generally, you should not directly use this class,
 * but instead use the XML::Document#find and XML::Node#find methods.
 *
 *  doc = XML::Document.string('<header>content</header>')
 *  context = XPath::Context.new(doc)
 *  context.node = doc.root
 *  context.register_namespaces_from_node(doc.root)
 *  nodes = context.find('/header')
 */

VALUE cXMLXPathContext;

static ID DOC_ATTRIBUTE;

static void rxml_xpath_context_free(xmlXPathContextPtr ctxt)
{
  xmlXPathFreeContext(ctxt);
}

static VALUE rxml_xpath_context_alloc(VALUE klass)
{
  return Data_Wrap_Struct(cXMLXPathContext, NULL, rxml_xpath_context_free, NULL);
}

/* call-seq:
 *    XPath::Context.new(node) -> XPath::Context
 *
 * Creates a new XPath context for the specified document.  The
 * context can then be used to evaluate an XPath expression.
 *
 *  doc = XML::Document.string('<header><first>hi</first></header>')
 *  context = XPath::Context.new(doc)
 *  nodes = XPath::Object.new('//first', context)
 *  nodes.length == 1
 */
static VALUE rxml_xpath_context_initialize(VALUE self, VALUE node)
{
  xmlDocPtr xdoc;
  VALUE document;

  if (rb_obj_is_kind_of(node, cXMLNode) == Qtrue)
  {
    document = rb_funcall(node, rb_intern("doc"), 0);
    if (NIL_P(document))
    rb_raise(rb_eTypeError, "Supplied node must belong to a document.");
  }
  else if (rb_obj_is_kind_of(node, cXMLDocument) == Qtrue)
  {
    document = node;
  }
  else
  {
    rb_raise(rb_eTypeError, "Supplied argument must be a document or node.");
  }

  Data_Get_Struct(document, xmlDoc, xdoc);
  DATA_PTR(self) = xmlXPathNewContext(xdoc);

  /* Save the doc as an attribute, this will expose it to Ruby's GC. */
  rb_ivar_set(self, DOC_ATTRIBUTE, document);

  return self;
}

/*
 * call-seq:
 *    context.register_namespace(prefix, uri) -> (true|false)
 *
 * Register the specified namespace URI with the specified prefix
 * in this context.

 *   context.register_namespace('xi', 'http://www.w3.org/2001/XInclude')
 */
static VALUE rxml_xpath_context_register_namespace(VALUE self, VALUE prefix, VALUE uri)
{
  xmlXPathContextPtr ctxt;
  Data_Get_Struct(self, xmlXPathContext, ctxt);

  /* Prefix could be a symbol. */
  prefix = rb_obj_as_string(prefix);
  
  if (xmlXPathRegisterNs(ctxt, (xmlChar*) StringValuePtr(prefix),
      (xmlChar*) StringValuePtr(uri)) == 0)
  {
    return (Qtrue);
  }
  else
  {
    /* Should raise an exception, IMHO (whose?, why shouldnt it? -danj)*/
    rb_warning("register namespace failed");
    return (Qfalse);
  }
}

/* call-seq:
 *    context.register_namespaces_from_node(node) -> self
 *
 * Helper method to read in namespaces defined on a node.
 *
 *  doc = XML::Document.string('<header><first>hi</first></header>')
 *  context = XPath::Context.new(doc)
 *  context.register_namespaces_from_node(doc.root)
 */
static VALUE rxml_xpath_context_register_namespaces_from_node(VALUE self,
    VALUE node)
{
  xmlXPathContextPtr xctxt;
  xmlNodePtr xnode;
  xmlNsPtr *xnsArr;

  Data_Get_Struct(self, xmlXPathContext, xctxt);

  if (rb_obj_is_kind_of(node, cXMLDocument) == Qtrue)
  {
    xmlDocPtr xdoc;
    Data_Get_Struct(node, xmlDoc, xdoc);
    xnode = xmlDocGetRootElement(xdoc);
  }
  else if (rb_obj_is_kind_of(node, cXMLNode) == Qtrue)
  {
    Data_Get_Struct(node, xmlNode, xnode);
  }
  else
  {
    rb_raise(rb_eTypeError, "The first argument must be a document or node.");
  }

  xnsArr = xmlGetNsList(xnode->doc, xnode);

  if (xnsArr)
  {
    xmlNsPtr xns = *xnsArr;

    while (xns)
    {
      /* If there is no prefix, then this is the default namespace.
       Skip it for now. */
      if (xns->prefix)
      {
        VALUE prefix = rb_str_new2((const char*)xns->prefix);
        VALUE uri = rb_str_new2((const char*)xns->href);
        rxml_xpath_context_register_namespace(self, prefix, uri);
      }
      xns = xns->next;
    }
    xmlFree(xnsArr);
  }

  return self;
}

static int iterate_ns_hash(st_data_t prefix, st_data_t uri, st_data_t self)
{
  rxml_xpath_context_register_namespace(self, prefix, uri);
  return ST_CONTINUE;
}

/*
 * call-seq:
 *    context.register_namespaces(["prefix:uri"]) -> self
 *
 * Register the specified namespaces in this context.  There are
 * three different forms that libxml accepts.  These include
 * a string, an array of strings, or a hash table:
 *
 *   context.register_namespaces('xi:http://www.w3.org/2001/XInclude')
 *   context.register_namespaces(['xlink:http://www.w3.org/1999/xlink',
 *                                'xi:http://www.w3.org/2001/XInclude')
 *   context.register_namespaces('xlink' => 'http://www.w3.org/1999/xlink',
 *                                  'xi' => 'http://www.w3.org/2001/XInclude')
 */
static VALUE rxml_xpath_context_register_namespaces(VALUE self, VALUE nslist)
{
  char *cp;
  long i;
  VALUE rprefix, ruri;

  /* Need to loop through the 2nd argument and iterate through the
   * list of namespaces that we want to allow */
  switch (TYPE(nslist))
  {
  case T_STRING:
    cp = strchr(StringValuePtr(nslist), (int) ':');
    if (cp == NULL)
    {
      rprefix = nslist;
      ruri = Qnil;
    }
    else
    {
      rprefix = rb_str_new(StringValuePtr(nslist), (int) ((long) cp
          - (long) StringValuePtr(nslist)));
      ruri = rb_str_new2(&cp[1]);
    }
    /* Should test the results of this */
    rxml_xpath_context_register_namespace(self, rprefix, ruri);
    break;
  case T_ARRAY:
    for (i = 0; i < RARRAY_LEN(nslist); i++)
    {
      rxml_xpath_context_register_namespaces(self, RARRAY_PTR(nslist)[i]);
    }
    break;
  case T_HASH:
    st_foreach(RHASH_TBL(nslist), iterate_ns_hash, self);
    break;
  default:
    rb_raise(
        rb_eArgError,
        "Invalid argument type, only accept string, array of strings, or an array of arrays");
  }
  return self;
}

/*
 * call-seq:
 *    context.node = node
 *
 * Set the current node used by the XPath engine

 *  doc = XML::Document.string('<header><first>hi</first></header>')
 *  context.node = doc.root.first
 */
static VALUE rxml_xpath_context_node_set(VALUE self, VALUE node)
{
  xmlXPathContextPtr xctxt;
  xmlNodePtr xnode;

  Data_Get_Struct(self, xmlXPathContext, xctxt);
  Data_Get_Struct(node, xmlNode, xnode);
  xctxt->node = xnode;
  return node;
}

/*
 * call-seq:
 *    context.find("xpath") -> true|false|number|string|XML::XPath::Object
 *
 * Executes the provided xpath function.  The result depends on the execution
 * of the xpath statement.  It may be true, false, a number, a string or 
 * a node set.
 */
static VALUE rxml_xpath_context_find(VALUE self, VALUE xpath_expr)
{
  xmlXPathContextPtr xctxt;
  xmlXPathObjectPtr xobject;
  xmlXPathCompExprPtr xcompexpr;
  VALUE result;

  Data_Get_Struct(self, xmlXPathContext, xctxt);

  if (TYPE(xpath_expr) == T_STRING)
  {
    VALUE expression = rb_check_string_type(xpath_expr);
    xobject = xmlXPathEval((xmlChar*) StringValueCStr(expression), xctxt);
  }
  else if (rb_obj_is_kind_of(xpath_expr, cXMLXPathExpression))
  {
    Data_Get_Struct(xpath_expr, xmlXPathCompExpr, xcompexpr);
    xobject = xmlXPathCompiledEval(xcompexpr, xctxt);
  }
  else
  {
    rb_raise(rb_eTypeError,
        "Argument should be an intance of a String or XPath::Expression");
  }

  if (xobject == NULL)
  {
    /* xmlLastError is different than xctxt->lastError.  Use
     xmlLastError since it has the message set while xctxt->lastError
     does not. */
    xmlErrorPtr xerror = xmlGetLastError();
    rxml_raise(xerror);
  }

  switch (xobject->type)
  {
  case XPATH_NODESET:
    result = rxml_xpath_object_wrap(xctxt->doc, xobject);
    break;
  case XPATH_BOOLEAN:
    result = (xobject->boolval != 0) ? Qtrue : Qfalse;
    xmlXPathFreeObject(xobject);
    break;
  case XPATH_NUMBER:
    result = rb_float_new(xobject->floatval);
    xmlXPathFreeObject(xobject);
    break;
  case XPATH_STRING:
    result = rb_str_new2((const char*)xobject->stringval);
    xmlXPathFreeObject(xobject);
    break;
  default:
    result = Qnil;
    xmlXPathFreeObject(xobject);
  }
  return result;
}

#if LIBXML_VERSION >= 20626
/*
 * call-seq:
 *    context.enable_cache(size = nil)
 *
 * Enables an XPath::Context's built-in cache.  If the cache is
 * enabled then XPath objects will be cached internally for reuse.
 * The size parameter controls sets the maximum number of XPath objects 
 * that will be cached per XPath object type (node-set, string, number,
 * boolean, and misc objects).  Set size to nil to use the default
 * cache size of 100.
 */
static VALUE
rxml_xpath_context_enable_cache(int argc,  VALUE *argv, VALUE self)
{
  xmlXPathContextPtr xctxt;
  VALUE size;
  int value = -1;

  Data_Get_Struct(self, xmlXPathContext, xctxt);

  if (rb_scan_args(argc, argv, "01", &size) == 1)
  {
	  value = NUM2INT(size);
  }

  if (xmlXPathContextSetCache(xctxt, 1, value, 0) == -1)
    rxml_raise(&xmlLastError);

  return self;
}

/*
 * call-seq:
 *    context.disable_cache
 * 
 * Disables an XPath::Context's built-in cache.
 */
static VALUE
rxml_xpath_context_disable_cache(VALUE self)
{
  xmlXPathContextPtr xctxt;
  Data_Get_Struct(self, xmlXPathContext, xctxt);

  if (xmlXPathContextSetCache(xctxt, 0, 0, 0) == -1)
    rxml_raise(&xmlLastError);

  return self;
}
#endif

void rxml_init_xpath_context(void)
{
  DOC_ATTRIBUTE = rb_intern("@doc");

  cXMLXPathContext = rb_define_class_under(mXPath, "Context", rb_cObject);
  rb_define_alloc_func(cXMLXPathContext, rxml_xpath_context_alloc);
  rb_define_attr(cXMLXPathContext, "doc", 1, 0);
  rb_define_method(cXMLXPathContext, "initialize", rxml_xpath_context_initialize, 1);
  rb_define_method(cXMLXPathContext, "register_namespaces", rxml_xpath_context_register_namespaces, 1);
  rb_define_method(cXMLXPathContext, "register_namespaces_from_node", rxml_xpath_context_register_namespaces_from_node, 1);
  rb_define_method(cXMLXPathContext, "register_namespace", rxml_xpath_context_register_namespace, 2);
  rb_define_method(cXMLXPathContext, "node=", rxml_xpath_context_node_set, 1);
  rb_define_method(cXMLXPathContext, "find", rxml_xpath_context_find, 1);
#if LIBXML_VERSION >= 20626
  rb_define_method(cXMLXPathContext, "enable_cache", rxml_xpath_context_enable_cache, -1);
  rb_define_method(cXMLXPathContext, "disable_cache", rxml_xpath_context_disable_cache, 0);
#endif
}
