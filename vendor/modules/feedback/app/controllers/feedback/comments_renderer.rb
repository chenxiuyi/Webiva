# Copyright (C) 2009 Pascal Rettig.

class Feedback::CommentsRenderer < ParagraphRenderer

  features '/feedback/comments_feature'

  paragraph :comments

  def comments
    @options = paragraph_options(:comments)

    if editor?
      @comment = Comment.new
      @comments = Comment.with_rating(@options.show).order_by_posted(@options.order.to_s).find(:all, :limit => 10)
      return render_paragraph :feature => :comments_page_comments
    end

    if @options.linked_to_type == 'page'
      content_link = [ paragraph.page_revision.revision_container_type, paragraph.page_revision.revision_container_id ]
    else
      connection_type, content_link = page_connection()
    end

    return(render_paragraph :inline => '') unless content_link

    logged_in = myself.id ? 'logged_in' : 'anonymous'
    display_string = "_#{content_link[0]}_#{content_link[1]}_#{logged_in}"
    
    result = renderer_cache(Comment, display_string, :skip => true ||  request.post? || @options.captcha) do |cache|
      @comment = Comment.new
      param_str = 'comment_' + paragraph.id.to_s
      if request.post? && params[param_str]
	if myself.id || @options.allowed_to_post == 'all'
	  @comment = Comment.new(:target_type => content_link[0],
				 :target_id => content_link[1],
				 :posted_ip => request.remote_ip,
				 :comment => params[param_str][:comment],
				 :name => myself.id ? myself.name : params[param_str][:name],
				 :end_user_id => myself.id)

	  @comment.captcha_invalid = true if @options.captcha && !simple_captcha_valid?
	  if @comment.save
	    target_cls = content_link[0].constantize
	    if(target_cls && target_cls.respond_to?("comment_posted"))
	      target_cls.comment_posted(content_link[1])
	    end
	    
	    if paragraph.update_action_count > 0
	      atr = @comment.attributes.slice('name','posted_ip','posted_at','comment')
	      atr['target'] = @comment.target.title if @comment.target && @comment.target.respond_to?(:title)
	      paragraph.run_triggered_actions(atr,'action',myself)
	    end

	    # Make sure we know if we posted after redirect
	    flash[:posted_comment] = true if @comment.id
	    
	    redirect_paragraph :page
	    return
	  end
	end
      end

      @comments = Comment.with_rating(@options.show).for_target(content_link[0],content_link[1]).order_by_posted(@options.order.to_s).find(:all)
      @posted_comment = flash[:posted_comment]
      cache[:output] = comments_page_comments_feature
    end
    
    render_paragraph :text => result.output
  end
end
